#include "stdafx.h"
#include "ElfRelocator.h"
#include "Core/Common.h"
#include "Core/Misc.h"
#include "Core/MathParser.h"
#include "Util/CRC.h"

struct ArFileHeader
{
	char fileName[16];
	char modifactionTime[12];
	char ownerId[6];
	char groupId[6];
	char fileMode[8];
	char fileSize[10];
	char magic[2];
};

struct ArFileEntry
{
	std::wstring name;
	ByteArray data;
};

static std::wstring toWLowercase(const std::string& str)
{
	std::wstring result;
	for (size_t i = 0; i < str.size(); i++)
	{
		result += tolower(str[i]);
	}

	return result;
}

std::wstring getFileNameFromPath(const std::wstring& path)
{
	int n = path.find_last_of(L"/\\");
	if (n == path.npos)
		return path;
	return path.substr(n);
}

std::vector<ArFileEntry> loadArArchive(const std::wstring& inputName)
{
	ByteArray input = ByteArray::fromFile(inputName);
	std::vector<ArFileEntry> result;

	if (memcmp(input.data(),"!<arch>\n",8) != 0)
	{
		if (memcmp(input.data(),"\x7F""ELF",4) != 0)
			return result;

		ArFileEntry entry;
		entry.name = getFileNameFromPath(inputName);
		entry.data = input;
		result.push_back(entry);
		return result;
	}

	int pos = 8;
	while (pos < input.size())
	{
		ArFileHeader* header = (ArFileHeader*) input.data(pos);
		pos += sizeof(ArFileHeader);
		
		// get file size
		int size = 0;
		for (int i = 0; i < 10; i++)
		{
			if (header->fileSize[i] == ' ')
				break;

			size = size*10;
			size += (header->fileSize[i]-'0');
		}

		// only ELF files are actually interesting
		if (memcmp(input.data(pos),"\x7F""ELF",4) == 0)
		{
			// get file name
			char fileName[17];
			fileName[16] = 0;
			for (int i = 0; i < 16; i++)
			{
				if (header->fileName[i] == ' ')
				{
					// remove trailing slashes of file names
					if (i > 0 && fileName[i-1] == '/')
						i--;
					fileName[i] = 0;
					break;;
				}

				fileName[i] = header->fileName[i];
			}
		
			ArFileEntry entry;
			entry.name = convertUtf8ToWString(fileName);
			entry.data = input.mid(pos,size);
			result.push_back(entry);
		}

		pos += size;
		if (pos % 2)
			pos++;
	}

	return result;
}

bool ElfRelocator::init(const std::wstring& inputName)
{
	relocator = Arch->getElfRelocator();
	if (relocator == NULL)
	{
		Logger::printError(Logger::Error,L"Object importing not supported for this architecture");
		return false;
	}

	auto inputFiles = loadArArchive(inputName);
	if (inputFiles.size() == 0)
	{
		Logger::printError(Logger::Error,L"Could not load library");
		return false;
	}

	for (ArFileEntry& entry: inputFiles)
	{
		ElfRelocatorFile file;

		ElfFile* elf = new ElfFile();
		if (elf->load(entry.data,false) == false)
		{
			Logger::printError(Logger::Error,L"Could not load object file %s",entry.name.c_str());
			return false;
		}

		if (elf->getType() != 1)
		{
			Logger::printError(Logger::Error,L"Unexpected ELF type %d in object file %s",elf->getType(),entry.name.c_str());
			return false;
		}

		if (elf->getSegmentCount() != 0)
		{
			Logger::printError(Logger::Error,L"Unexpected segment count %d in object file %s",elf->getSegmentCount(),entry.name.c_str());
			return false;
		}

		// load all relevant sections of this file
		for (int s = 0; s < elf->getSegmentlessSectionCount(); s++)
		{
			ElfSection* sec = elf->getSegmentlessSection(s);
			if (!(sec->getFlags() & SHF_ALLOC))
				continue;

			if (sec->getType() == SHT_PROGBITS || sec->getType() == SHT_NOBITS)
			{
				ElfRelocatorSection sectionEntry;
				sectionEntry.section = sec;
				sectionEntry.index = s;
				sectionEntry.relSection = NULL;

				// search relocation section
				for (int k = 0; k < elf->getSegmentlessSectionCount(); k++)
				{
					ElfSection* relSection = elf->getSegmentlessSection(k);
					if (relSection->getType() != SHT_REL)
						continue;
					if (relSection->getInfo() != s)
						continue;

					// got it
					sectionEntry.relSection = relSection;
					break;
				}

				file.sections.push_back(sectionEntry);
			}
		}

		// init exportable symbols
		for (int i = 0; i < elf->getSymbolCount(); i++)
		{
			ElfRelocatorSymbol symEntry;

			Elf32_Sym* symbol = elf->getSymbol(i);
			symEntry.type = symbol->st_info & 0xF;

			if (symEntry.type == STT_OBJECT || symEntry.type == STT_FUNC)
			{
				symEntry.name = toWLowercase(elf->getStrTableString(symbol->st_name));
				symEntry.relativeAddress = symbol->st_value;
				symEntry.section = symbol->st_shndx;
				symEntry.size = symbol->st_size;
				symEntry.label = NULL;

				file.symbols.push_back(symEntry);
			}
		}

		file.elf = elf;
		file.name = entry.name;
		files.push_back(file);
	}

	return true;
}

bool ElfRelocator::exportSymbols()
{
	bool error = false;

	for (ElfRelocatorFile& file: files)
	{
		for (ElfRelocatorSymbol& sym: file.symbols)
		{
			if (sym.label != NULL)
				continue;

			sym.label = Global.symbolTable.getLabel(sym.name,-1,-1);
			if (sym.label == NULL)
			{
				Logger::printError(Logger::Error,L"Invalid label name \"%s\"",sym.name.c_str());
				error = true;
				continue;
			}

			if (sym.label->isDefined())
			{
				Logger::printError(Logger::Error,L"Label \"%s\" already defined",sym.name.c_str());
				error = true;
				continue;
			}

			RelocationData data;
			data.symbolAddress = sym.relativeAddress;
			relocator->setSymbolAddress(data,sym.relativeAddress,sym.type);

			sym.relativeAddress = data.symbolAddress;
			sym.label->setInfo(data.targetSymbolInfo);
			sym.label->setUpdateInfo(false);

			sym.label->setValue(0);
			sym.label->setDefined(true);
		}
	}

	return !error;
}

bool ElfRelocator::relocateFile(ElfRelocatorFile& file, int& relocationAddress)
{
	ElfFile* elf = file.elf;
	int start = relocationAddress;

	// calculate address for each section
	std::map<int,int> relocationOffsets;
	for (ElfRelocatorSection& entry: file.sections)
	{
		ElfSection* section = entry.section;
		int index = entry.index;
		int size = section->getSize();

		while (relocationAddress % section->getAlignment())
		{
			relocationAddress++;
		}

		relocationOffsets[index] = relocationAddress;
		relocationAddress += size;
	}

	int dataStart = outputData.size();
	outputData.reserveBytes(relocationAddress-start);

	// load sections
	bool error = false;
	for (ElfRelocatorSection& entry: file.sections)
	{
		ElfSection* section = entry.section;
		int index = entry.index;

		if (section->getType() == SHT_NOBITS)
		{
			// reserveBytes initialized the data to 0 already
			continue;
		}
		
		ByteArray sectionData = section->getData();

		// relocate if necessary
		ElfSection* relSection = entry.relSection;
		if (relSection != NULL)
		{
			Elf32_Rel* rel = (Elf32_Rel*) &relSection->getData()[0];
			int relCount = relSection->getSize()/sizeof(Elf32_Rel);

			for (int i = 0; i < relCount; i++)
			{
				int pos = rel[i].r_offset;

				int symNum = rel[i].getSymbolNum();
				if (symNum <= 0)
				{
					Logger::queueError(Logger::Warning,L"Invalid symbol num %06X",symNum);
					error = true;
					continue;
				}

				auto sym = elf->getSymbol(symNum);
				int symSection = sym->st_shndx;
				
				RelocationData relData;
				relData.opcode = sectionData.getDoubleWord(pos);
				relData.opcodeOffset = pos+relocationOffsets[index];
				relocator->setSymbolAddress(relData,sym->st_value,sym->st_info & 0xF);

				// externs?
				if (relData.targetSymbolType == STT_NOTYPE && sym->st_shndx == 0)
				{
					std::wstring symName = toWLowercase(elf->getStrTableString(sym->st_name));

					Label* label = Global.symbolTable.getLabel(symName,-1,-1);
					if (label == NULL)
					{
						Logger::queueError(Logger::Error,L"Invalid external symbol %s",symName.c_str());	
						error = true;
						continue;
					}
					if (label->isDefined() == false)
					{
						Logger::queueError(Logger::Error,L"Undefined external symbol %s",symName.c_str());
						error = true;
						continue;
					}
					
					relData.relocationBase = label->getValue();
					relData.targetSymbolType = label->isData() ? STT_OBJECT : STT_FUNC;
					relData.targetSymbolInfo = label->getInfo();
				} else {
					relData.relocationBase = relocationOffsets[symSection]+relData.symbolAddress;
				}

				if (relocator->relocateOpcode(rel[i].getType(),relData) == false)
				{
					Logger::queueError(Logger::Error,relData.errorMessage);
					error = true;
					continue;
				}

				sectionData.replaceDoubleWord(pos,relData.opcode);
			}
		}

		int arrayStart = dataStart+relocationOffsets[index]-start;
		memcpy(outputData.data(arrayStart),sectionData.data(),sectionData.size());
	}
	
	// now update symbols
	for (ElfRelocatorSymbol& sym: file.symbols)
	{
		int oldAddress = sym.relocatedAddress;

		switch (sym.section)
		{
		case SHN_ABS:		// address does not change
			sym.relocatedAddress = sym.relativeAddress;
			break;
		case SHN_COMMON:	// needs to be allocated. relativeAddress gives alignment constraint
			{
				int start = relocationAddress;

				while (relocationAddress % sym.relativeAddress)
					relocationAddress++;

				sym.relocatedAddress = relocationAddress;
				relocationAddress += sym.size;
				outputData.reserveBytes(relocationAddress-start);
			}
			break;
		default:			// normal relocated symbol
			sym.relocatedAddress = sym.relativeAddress+relocationOffsets[sym.section];
			break;
		}

		if (sym.label != NULL)
			sym.label->setValue(sym.relocatedAddress);

		if (oldAddress != sym.relocatedAddress)
			dataChanged = true;
	}

	return !error;
}

bool ElfRelocator::relocate(int& memoryAddress)
{
	int oldCrc = getCrc32(outputData.data(),outputData.size());
	outputData.clear();
	dataChanged = false;

	bool error = false;
	int start = memoryAddress;

	for (ElfRelocatorFile& file: files)
	{
		if (relocateFile(file,memoryAddress) == false)
			error = true;
	}
	
	int newCrc = getCrc32(outputData.data(),outputData.size());
	if (oldCrc != newCrc)
		dataChanged = true;

	memoryAddress -= start;
	return !error;
}

void ElfRelocator::writeSymbols(SymbolData& symData)
{
	for (ElfRelocatorFile& file: files)
	{
		for (ElfRelocatorSymbol& sym: file.symbols)
		{
			symData.addLabel(sym.relocatedAddress,sym.name.c_str());

			switch (sym.type)
			{
			case STT_OBJECT:
				symData.addData(sym.relocatedAddress,sym.size,SymbolData::Data8);
				break;
			case STT_FUNC:
				Global.symData.startFunction(sym.relocatedAddress);
				Global.symData.endFunction(sym.relocatedAddress+sym.size);
				break;
			}
		}
	}
}