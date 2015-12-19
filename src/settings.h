#ifndef __SETTING_H__
#define __SETTING_H__

#include <windows.h>
#include <winbase.h>
#include <iostream>
#include <string>
#include <stdio.h>
#include <tchar.h>

#define MAXKEYLENGHT 255
#define MAXPATH 512

//#define _MYDEBUG // Activate a debug console

#ifdef __cplusplus
inline void null_func(char *format, ...) { }
#define DbgEmpty null_func
#define TbgPrintf null_func
#else
#define DbgEmpty { }
#endif

#ifdef  _MYDEBUG
/* Debug Trace Enabled */
#include <stdio.h>
#define DbgPrintf printf
#define TbgPrintf _tprintf
#else
/* Debug Trace Disabled */
#define DbgPrintf DbgEmpty
#endif

template <class T> void SafeRelease(T **ppT)
{
	if (*ppT)
	{
		(*ppT)->Release();
		*ppT = NULL;
	}
}

class Settings
{
public:
	Settings(const TCHAR * ini_name);					    	// Application inifile name....
	void set(const TCHAR *  section, const TCHAR *  key, const TCHAR *  value); // TCHAR string
	void set_int(const TCHAR *  section, const TCHAR *  key, const int value);  // int
	TCHAR* get(const TCHAR * section, const TCHAR * key, const TCHAR * defaultvalue);
	const int get_int(const TCHAR * section, const TCHAR * key, const int defaultvalue);
//private:
	TCHAR filename[MAXPATH];
	TCHAR tResult[255];
};



#endif __SETTING_H__
