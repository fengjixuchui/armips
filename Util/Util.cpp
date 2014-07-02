#include "stdafx.h"
#include "Util.h"
#include <sys/stat.h>

std::wstring convertUtf8ToWString(const char* source)
{
	std::wstring result;

	int index = 0;
	while (source[index] != 0)
	{
		int extraBytes = 0;
		int value = source[index++];
			
		if ((value & 0xE0) == 0xC0)
		{
			extraBytes = 1;
			value &= 0x1F;
		} else if ((value & 0xF0) == 0xE0)
		{
			extraBytes = 2;
			value &= 0x0F;
		} else if (value > 0x7F)
		{
			// error
			return std::wstring();
		}

		for (int i = 0; i < extraBytes; i++)
		{
			int b = source[index++];
			if ((b & 0xC0) != 0x80)
			{
			// error
			return std::wstring();
			}

			value = (value << 6) | (b & 0x3F);
		}

		result.push_back(value);
	}

	return result;
}

std::string convertWStringToUtf8(const std::wstring& source)
{
	std::string result;
	
	for (size_t i = 0; i < source.size(); i++)
	{
		wchar_t character = source[i];
		if (character < 0x80)
		{
			result.push_back(character & 0x7F);
		} else if (character < 0x800)
		{
			result.push_back(0xC0 | (character >> 6) & 0x1F);
			result.push_back(0x80 | (character & 0x3F));
		} else {
			result.push_back(0xE0 | (character >> 12) & 0xF);
			result.push_back(0x80 | ((character >> 6) & 0x3F));
			result.push_back(0x80 | (character & 0x3F));
		}
	}

	return result;
}

std::wstring intToHexString(unsigned int value, int digits, bool prefix)
{
	std::wstring result;
	result.reserve((digits+prefix) ? 2 : 0);

	if (prefix)
	{
		result.push_back('0');
		result.push_back('x');
	}

	while (digits > 8)
	{
		result.push_back('0');
		digits--;
	}
	
	wchar_t buf[9];
	swprintf(buf,9,L"%0*X",digits,value);
	result += buf;

	return result;
}

std::wstring intToString(unsigned int value, int digits)
{
	std::wstring result;
	result.reserve(digits);

	while (digits > 8)
	{
		result.push_back(' ');
		digits--;
	}
	
	wchar_t buf[9];
	swprintf(buf,9,L"%*d",digits,value);
	result += buf;

	return result;
}

StringList getStringListFromArray(wchar_t** source, int count)
{
	StringList result;
	for (int i = 0; i < count; i++)
	{
		result.push_back(std::wstring(source[i]));
	}

	return result;
}


int fileSize(const std::wstring& fileName)
{
	struct _stat fileStat; 
	int err = _wstat(fileName.c_str(), &fileStat ); 
	if (0 != err) return 0; 
	return fileStat.st_size; 
}

int fileSize(const std::string& fileName)
{
	struct _stat fileStat; 
	int err = _stat(fileName.c_str(), &fileStat ); 
	if (0 != err) return -1; 
	return fileStat.st_size; 
}

bool fileExists(const std::wstring& strFilename)
{
	struct _stat stFileInfo;
	int intStat = _wstat(strFilename.c_str(),&stFileInfo);
	return intStat == 0;
}

bool fileExists(const std::string& strFilename)
{
	struct _stat stFileInfo;
	int intStat = _stat(strFilename.c_str(),&stFileInfo);
	return intStat == 0;
}

std::wstring toWLowercase(const std::string& str)
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
