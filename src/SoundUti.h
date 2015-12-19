#if !defined SOUNDUTI_H
#define SOUNDUTI_H
#include <tchar.h>
#include "ExtIO_H101.h"
#include "settings.h"
#include "EndpointVolume.h"
#include <mmdeviceapi.h>
#include <commctrl.h>

#define MAXTCHAR (200)


int SetVolume(UINT idx, float valuex100);

int Soundsetup(TCHAR * name);

int SoundPlaySetup(TCHAR * name);

#endif
