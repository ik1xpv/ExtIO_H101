
#include "Settings.h"

Settings::Settings(const TCHAR * ini_name)
{
   GetEnvironmentVariable(TEXT("APPDATA"), filename, sizeof filename);
   _tcscat_s(filename, ini_name);
}

void Settings::set(const TCHAR *  section, const TCHAR *  key, const TCHAR *  value)
{
	WritePrivateProfileString(section, key , value, filename);
}

void Settings::set_int(const TCHAR *  section, const TCHAR *  key, const int value)
{
	TCHAR data[255];
	_stprintf( data, TEXT("%d"), value);
	WritePrivateProfileString(section, key, data, filename);
}

TCHAR * Settings::get(const TCHAR * section, const TCHAR * key, const TCHAR * defaultvalue)
{
	GetPrivateProfileString(section, key, defaultvalue, tResult, sizeof(tResult), filename);
	return tResult;
}

const int Settings::get_int(const TCHAR * section, const TCHAR * key, const int defaultvalue)
{
	TCHAR default[255];
	int dato; 
	_stprintf( default, TEXT("%d"),  defaultvalue);
	GetPrivateProfileString(section, key, default, tResult, sizeof(tResult), filename);
	_stscanf(tResult, TEXT("%d"), &dato);
	return dato;
}


