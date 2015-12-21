/* ******************************************************************************
 * ExtIO_H101.dll 
 * Copyleft by Oscar Steila IK1XPV [ik1xpv at gmail.com]
 * per GNU I think I'm required to leave the following here:
 *
 * Based on original work:
 * Winrad specifications for the external I/O DLL by Alberto di Bene I2PHD
 * see: www.winrad.org/bin/Winrad_Extio.pdf
 * see Google for other variations 
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <Windows.h>
#include <WindowsX.h>
#include <commctrl.h>
#include <process.h>
#include <tchar.h>
#include <new>
#include <stdio.h>
#include "rtaudio.h"
#include "resource.h"
#include "ExtIO_H101.h"
#include "fifo.h"
#include "Udefines.h"
#include "freqtabH101.h"
#include "settings.h"
#include "SoundUti.h"


HMODULE hInst;

BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
	)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		hInst = hModule;
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}


// PPM correction parameters
#define MAX_PPM	1000
#define MIN_PPM	-1000
#define MAX_ADC	100
#define MIN_ADC	1

static int ppm_default=0;
static double freq_correction = 1.0;
static int adcx_default = 50;
static long gfreq = 0;

// sample rate 
typedef struct sr {
	int value;
	TCHAR *name;
} sr_t;

static sr_t samplerates[] = {
	{  48000, TEXT("48 kHz") }
};
static int samplerate_default=0; // 48 kHz

// H101 UART over audio channel 
#define SERIAL1     ( 0.5f )
#define SERIAL0		(-0.5f )

// Local Oscillator frequency
static long LOfreq;
// DC filter 
static float averageI = 0.0f;
static float averageQ = 0.0f;
static float ratio = 0.035f;
static float oneMinusRatio  = 1.0f - ratio;
static float cgain = 1.0f / (float)0x7fff;

// antennas
static  TCHAR*  antenna[] = { //antenna selector
	TEXT("ANT 1"),
	TEXT("ANT 2")
};
static int antenna_idx = 0;

// attenuator
typedef struct att {
	int value;
	TCHAR *name;
} att_t;

static att_t attenuation[] = {
	{ 0, TEXT("     0") },
	{ 1, TEXT("  - 10") },
	{ 2, TEXT("  - 20") }
};

static int attenuation_idx = 0; // 0 dB
// IF GAIN
static att_t ifgain[] = {
	{ 0, TEXT("  - 10") },
	{ 1, TEXT("     0") },
	{ 2, TEXT("  + 20") },
	{ 3, TEXT("  + 40") }
};

static int ifgain_idx = 2; // 0 dB

const float gainadj[3][4] = {
// H101 ifgain 
// -10       0        +20      +40   dB
{  17.0f ,  +7.0f , -13.0f , -33.0f },   // att = 0 dB
{ +27.0f , +17.0f , -3.0f  , -23.0f },   // att = -10 dB
{ +37.0f , +27.0f ,  +7.0f , -13.0f }    // att = -20 dB

};


// past io data
static UINT8 _iodata[3];

// Audio devices
#define MAX_AUDIODEVICES (64)

typedef struct soundport {
	int index;
	TCHAR name[255];
	UINT output;
	UINT input;
	bool usb;
	UINT rate;
} sound_t;

static std::vector <sound_t> soundPorts;

/* ExtIO Callback */
static void (* WinradCallBack)(int, int, float, void *) = NULL;

#define WINRAD_LOCHANGE 101
#define WINRAD_STOP 108
// Dialog callback
static INT_PTR CALLBACK MainDlgProc(HWND, UINT, WPARAM, LPARAM);
HWND h_dialog=NULL;

int H101CallBack(void *outputBuffer, void *inputBuffer, unsigned int nBufferFrames,
	double streamTime, RtAudioStreamStatus status, void *data);
// Audio
RtAudio * adacp = nullptr;  // RtAudio class pointer
static int buffer_len = 1024;// audio buffer len
static float *float_buf = nullptr; // Buffer float pointer
static float onesampledelay; // one sample delay to compensate PCM2902 bug
static int deviceAdcId =-1;

static FifoUchar uartfifo;  // uchar fifo to uart audio
static UINT uartbitcnt = 0; // bit count
static UINT idxbit = 0;     // bit index
static UINT uartsignal = 0xffff;  // uart char bit vector

#define WriteChar  uartfifo.sampleIn  /* put data to uart fifo */

static int ddsfreq = 0; // dds frequency buffer 
static int ddsMHZ = 0;  // dds MHz buffer
static bool setEditTextProgramatically = true;

void DDSfreq(int fdds, bool doit);	// program frequency
void WriteIOH101(void); // program IO
void SendPacket(UINT8 *ptdata, UINT16 tdatalen); // prepare and send uart packet 

static Settings H101settings(TEXT("\\ExtIO_H101_dll.ini"));



// string to TCHAR
TCHAR* string2TCHAR(std::string str) {
	TCHAR* dest = (TCHAR*)malloc(sizeof(TCHAR) * (str.length() + 1));
	std::wstring wsTmp(str.begin(), str.end());
	if (dest == nullptr) exit(1);
	_tcscpy_s((TCHAR *) dest, (str.length() + 1), wsTmp.c_str());
	return dest;
}


// enumerate sound devices using RtAudio
bool EnumerateSounds(void)
{
	//  Enumerate audio devices
	RtAudio audio;
	soundPorts.clear();
	soundPorts.reserve(MAX_AUDIODEVICES);
	// Determine the number of devices available
	unsigned int devices = audio.getDeviceCount();
	// Scan through devices for various capabilities
	RtAudio::DeviceInfo info;
	sound_t soundx;
	bool r = false;
	for (unsigned int i = 0; i < devices; i++)
	{
		info = audio.getDeviceInfo(i);
		
		if (info.probed == true) {
			soundx.index = i;
			memcpy(soundx.name, string2TCHAR(info.name), sizeof(soundx.name));
			soundx.output = info.outputChannels;
			soundx.input = info.inputChannels;
			soundx.usb = (strstr(info.name.c_str(), "USB") != nullptr);
			soundPorts.push_back(soundx);

			DbgPrintf("%d %s \n", soundx.index, info.name);

			r = true;
		}
	}
	return r;
}

void AdviceSoundIn()
{
	TCHAR msgstr[512];
	TCHAR tstri[255];
	ComboBox_GetText((GetDlgItem(h_dialog, IDC_AUDIOIN)), tstri, sizeof(tstri) / sizeof(TCHAR));
	_stprintf_s(msgstr, 512, TEXT("Please\n-open Sound setting (search audio)\n-press the recording tab\n-select the %s device\n-press the properties button\n-press advanced tab\n-select 2 channel, 16 bit, 48KHz (DVD quality) format\n-press levels tab and set volume at maximum (100)\n-restart the SDR program."), tstri);
	MessageBox(NULL, msgstr, TEXT("The audio device setting is required"), MB_ICONINFORMATION);
}

void AdviceSoundOut()
{
	TCHAR msgstr[512];
	TCHAR tstro[255];
	ComboBox_GetText((GetDlgItem(h_dialog, IDC_AUDIOOUT)), tstro, sizeof(tstro) / sizeof(TCHAR));
	_stprintf_s(msgstr, 512, TEXT("Please\n-open Audio setting (search audio)\n-select the %s device\n-press the properties button\n-press advanced tab\n-select 2 channel, 16 bit, 48KHz (DVD quality) format\n-press levels tab and set volume at maximum (100)\n-restart the SDR program."), tstro);
	MessageBox(NULL, msgstr, TEXT("The audio device setting is required"), MB_ICONINFORMATION);
}



/*
InitHW

bool __stdcall __declspec(dllexport) InitHW(char *name, char *model, int& type)

This entry is the first called by Winrad at startup time, and it is used both to tell to the DLL that it is time to
initialize the hardware, and to get back a descriptive name and model (or Serial Number) of the HW, together with
a type code.

Parameters :
name - descriptive name of the hardware.Preferably not longer than about 16 characters, as it will be used
in a Winrad menu.
model - model code of the hardware, or its Serial Number.Keep also this field not too long, for the same
reason of the previous one.
type - this is an index code that Winrad uses to identify the hardware type supported by the  DLL.
value : 3 - the hardware does its own digitization and the audio data are returned to Winrad
via the callback device.Data must be in 16?bit  (short) format, little endian.
Return value :
 true - everything went well, the HW did initialize, and the return parameters have been filled.
 false - the HW did not initialize (error, or powered off, or other reasons).
*/

extern "C"
bool  LIBRTL_API __stdcall InitHW(char *name, char *model, int& type)
{
	static bool first = true;

#ifdef _MYDEBUG
	if (AllocConsole())
	{
		FILE* f;
		freopen_s(&f, "CONOUT$", "wt", stdout);
		SetConsoleTitle(TEXT("Debug Console H101"));
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_RED);
		DbgPrintf("IK1XPV fecit MMXV\n");
	}
#endif
	DbgPrintf("\n-InitHW\n");
	strcpy_s(name, 16, "CIAOradio");	// change with the name of your HW
	strcpy_s(model, 16, "H101");	// change with the model of your HW
	type = 7; //the hardware does its own digitization.. float32
	if (first)
	{
		first = false;
		LOfreq = -1; // just a default value
		memset(_iodata, 0, sizeof(_iodata));
	}
	if (!EnumerateSounds())
		return false;
	float_buf = (float *) new (std::nothrow) float[buffer_len * 2];  //allocate buffer
	WriteChar(SYN);  //activate uart
	return true;
}
/*
GetStatus
int __stdcall __declspec(dllexport) GetStatus(void)
This entry point is meant to allow the DLL to return a status information to Winrad, upon request. Presently it is
never called by Winrad, though its existence is checked when the DLL is loaded. So it must implemented, even if in
a dummy way. It is meant for future expansions, for complex HW that implement e.g. a preselector or some other
controls other than a simple LO frequency selection.
The return value is an integer that is application dependent.
*/
extern "C"
int LIBRTL_API __stdcall GetStatus()
{
	DbgPrintf("GetStatus\n");
	/* dummy function */
	return 0;
}
/*
OpenHW
bool __stdcall __declspec(dllexport) OpenHW(void)
This entry is called by Winrad each time the user specifies that Winrad should receive its audio data input through
the hardware managed by this DLL, or, if still using the sound card for this, that the DLL must activate the control
of the external hardware. It can be used by the DLL itself for delayed init tasks, like, e.g., the display of its own GUI,
if the DLL has one.
It has no parameters.
Return value :
 true - everything went well. 
 false - some error occurred, the external HW cannot be controlled by the DLL code.
*/
extern "C"
bool  LIBRTL_API __stdcall OpenHW()
{
	DbgPrintf("OpenHW\n");
	int r = 0;

	if (r < 0)
		return FALSE;
	h_dialog = CreateDialog(hInst, MAKEINTRESOURCE(IDD_RTL_SETTINGS), NULL, (DLGPROC)MainDlgProc);
	ShowWindow(h_dialog, SW_HIDE);
	TCHAR tstri[MAXTCHAR];
	TCHAR tstro[MAXTCHAR];
	ComboBox_GetText((GetDlgItem(h_dialog, IDC_AUDIOIN)), tstri, sizeof(tstri) / sizeof(TCHAR));
	ComboBox_GetText((GetDlgItem(h_dialog, IDC_AUDIOOUT)), tstro, sizeof(tstro) / sizeof(TCHAR));
	if (((_tcsstr(tstri, TEXT("Microphone")) == NULL) && (_tcsstr(tstri, TEXT("Microfono")) == NULL)) ||
		(_tcsstr(tstri, TEXT("USB Audio CODEC")) == NULL) ||
		((_tcsstr(tstro, TEXT("Speakers")) == NULL) && (_tcsstr(tstro, TEXT("Altoparlanti")) == NULL))||
		(_tcsstr(tstro, TEXT("USB Audio CODEC")) == NULL))
		{
			MessageBox(NULL, 
TEXT("Is H101 connected ? \nplease verify and press ok to exit\n\nH101 sound devices are: \nMicrophone (USB Audio CODEC) o Microfono (USB Audio CODEC)\nSpeakers (USB Audio CODEC) o Altoparlanti (USB Audio CODEC)"),
				TEXT("Duplex USB audio device not found"), MB_ICONQUESTION);
			ExitProcess(0);  // restart required
		}

	return TRUE;
}

/*
SetHWLO
int __stdcall __declspec(dllexport) SetHWLO(long LOfreq)
This entry point is used by Winrad to communicate and control the desired frequency of the external HW via the
DLL. The frequency is expressed in units of Hz. The entry point is called at each change (done by any means) of the
LO value in the Winrad main screen. 
Parameters :
LOfreq
a long integer specifying the frequency the HW LO should be set to, expressed in Hz.
Return values : 0 - The function did complete without errors.
< 0 (a negative number N) - The specified frequency is lower than the minimum that the hardware is capable to generate. The
absolute value of N indicates what is the minimum supported by the HW.
> 0 (a positive number N) - The specified frequency is greater than the maximum that the hardware is capable to generate. The
value of N indicates what is the maximum supported by the HW.
*/
extern "C"
long LIBRTL_API __stdcall SetHWLO(long freq)
{
	int freque;
	gfreq = freq;
	DbgPrintf("SetHWLO %d\n", freq);
	double nominal =(double) freq * freq_correction;
	LOfreq = getfreq((int) nominal, TRUE, &freque); 

	DDSfreq((int) LOfreq, TRUE);

	if (LOfreq !=freq )    
		WinradCallBack(-1,WINRAD_LOCHANGE,0,NULL);  // signal the real LO is different
	
	return 0;
}
/*
 StartHW
 
 int __stdcall __declspec(dllexport) StartHW(long freq)

 This entry is called by Winrad each time the user presses the Start button on the Winrad main screen, after having
 previously specified that the DLL is in control of the external hardware.
 Parameters :
 freq - an integer specifying the frequency the HW should be set to, expressed in Hz.
 Return value :
 An integer specifying how many I/Q pairs are returned by the DLL each time the callback function is
 invoked (see later). This information is used of course only when the input data are not coming from the
 sound card, but through the callback device.
 If the number is negative, that means that an error has occurred,  Winrad interrupts the starting process and
 returns to the idle status. The number of I/Q pairs must be at least 512, or an integer multiple of that value
*/
extern "C"
int LIBRTL_API __stdcall StartHW(long freq)
{
	DbgPrintf("StartHW\n");
	int k,w;
	gfreq = freq;
	adacp = new RtAudio;
	// Set the same number of samples for both input and output.
	unsigned int bufferBytes, bufferFrames = buffer_len;
	RtAudio::StreamParameters iParams, oParams;


	TCHAR tstri[255];
	TCHAR tstro[255];
	ComboBox_GetText((GetDlgItem(h_dialog, IDC_AUDIOIN)), tstri,sizeof(tstri)/sizeof(TCHAR));
	ComboBox_GetText((GetDlgItem(h_dialog, IDC_AUDIOOUT)), tstro, sizeof(tstro) / sizeof(TCHAR));
	k = 0;
	w = 0;
	for (unsigned int i = 0; i < soundPorts.size(); i++)
	{
		if (_tcscmp(soundPorts[i].name, tstri) == 0)
		{
			w++;
			iParams.deviceId = soundPorts[i].index;
			if (soundPorts[i].input > 1) 
				k++;
			else
			{
				if (Soundsetup(soundPorts[i].name) == 0)
					k++;
				else
					AdviceSoundIn();
			}
				
		}
		if (_tcscmp(soundPorts[i].name, tstro) == 0)
		{
			w++;
			oParams.deviceId = soundPorts[i].index;
			if (soundPorts[i].output > 1)
				k++;
			else
			{
				if (SoundPlaySetup(soundPorts[i].name) == 0)
					k++;
				else
					AdviceSoundOut();
			}
		}
	}
	if (w != 2)
		MessageBox(NULL, TEXT("Is H101 connected ? \nplease verify and press ok to exit\n"),
			TEXT("Duplex USB audio device not found"), MB_ICONQUESTION);
	if (k != 2)
		ExitProcess(0);  // restart required

	SetVolume(iParams.deviceId, (float) adcx_default); 
	SetVolume(oParams.deviceId, 100.0f);

	iParams.nChannels = 2;
	oParams.nChannels = 2;

	int srate_idx = ComboBox_GetCurSel(GetDlgItem(h_dialog, IDC_SAMPLERATE));
	try {
		adacp->openStream(&oParams, &iParams, RTAUDIO_FLOAT32, samplerates[srate_idx].value, &bufferFrames, &H101CallBack, (void *)&bufferBytes);
	}
	catch (RtAudioError& e) {
		e.printMessage();
		if (adacp->isStreamOpen()) adacp->closeStream();
		return -1;
	}

	bufferBytes = bufferFrames * 2 * 4;

	try {
		adacp->startStream();
	}
	catch (RtAudioError& e) {
		e.printMessage();
		return -1;
	}
	DbgPrintf("\naudioprocessor started \n");
	
	{  // init H101
		UINT8 writebuf[4];  
		writebuf[0] = CTRL_RX;				    //CTRL byte
		writebuf[1] = AT_CONTROLINIT;			//CMD  byte
		writebuf[2] = 0;
		SendPacket((unsigned char*)writebuf, 2);
	}
//	Sleep(200);
	SetHWLO(freq);

	deviceAdcId = iParams.deviceId; // salva idx audio in
	return buffer_len;
}


/*
GetHWLO
long __stdcall __declspec(dllexport) GetHWLO(void)
This entry point is meant to query the external hardware?s set frequency via the DLL.. It is  used by Winrad to
handle a asynchronous status of 101 (see below the callback device), but not checked at startup for its presence.
The return value is the current LO frequency, expressed in units of Hz.
*/
extern "C"
long LIBRTL_API __stdcall GetHWLO()
{
	double nominal = (double)LOfreq / freq_correction;
	DbgPrintf("GetHWLO %e \n", nominal);
	return (long) nominal;
}

/*
GetHWSR
long __stdcall __declspec(dllexport) GetHWSR(void)
This entry point is used to ask the external DLL which is the current value of the sampling rate. If the sampling rate
is changed either by means of a hardware action or because the user specified a new sampling rate in the GUI of the
DLL, Winrad must be informed by using the callback device (described below). 
The return value is the value of the current sampling rate expressed in units of Hz.
*/
extern "C"
long LIBRTL_API __stdcall GetHWSR()
{
	long srate = 48000;
	int srate_idx = ComboBox_GetCurSel(GetDlgItem(h_dialog, IDC_SAMPLERATE));
	if (srate_idx >= 0 && srate_idx < (sizeof(samplerates) / sizeof(samplerates[0])))
	{
		srate = (long) samplerates[srate_idx].value;
	}
	DbgPrintf("GetHWSR   %d\n", srate);
	return srate;
}

/*

StopHW
void __stdcall __declspec(dllexport) StopHW(void)
This entry is called by Winrad each time the user presses the Stop button on the Winrad main screen. It can be used
by the DLL for whatever task might be needed in such an occurrence. If the external HW does not provide the
audio data, being, e.g., just a DDS or some other sort of an oscillator, typically this call is a No?op. The DLL could
also use this call to hide its GUI, if any.
If otherwise the external HW sends the audio data via the USB port, or any other hardware port managed by the
DLL, when this entry is called, the HW should be commanded by the DLL to stop sending data.
It has no parameters and no return value.

*/
extern "C"
void LIBRTL_API __stdcall StopHW()
{
	DbgPrintf("StopHW\n");
	if (adacp != nullptr)
	{ 
		adacp->stopStream();
		delete adacp;
		adacp = nullptr;
	}
}


/*
CloseHW
void __stdcall __declspec(dllexport) CloseHW(void)
 
 This entry is called by Winrad when the User indicates that the control of the external HW is no longer needed or
 wanted. This is done in Winrad by choosing ShowOptions | Select Input then selecting either WAV file or Sound
 Card. The DLL can use this information to e.g. shut down its GUI interface, if any, and possibly to put the
 controlled HW in a idle status.
 It has no parameters and no return value.
*/
extern "C"
void LIBRTL_API __stdcall CloseHW()
{
	DbgPrintf("CloseHW\n");
	if (float_buf != nullptr)
		delete(float_buf);
	if (h_dialog!= nullptr)
		DestroyWindow(h_dialog);
}

/*
ShowGUI
void __stdcall __declspec(dllexport) ShowGUI(void)
This entry point is used by Winrad to tell the DLL that the user did ask to see the GUI of the DLL itself, if it has one.
The implementation of this call is optional 
It has  no return value.
*/
extern "C"
void LIBRTL_API __stdcall ShowGUI()
{
	DbgPrintf("ShowGUI\n");
	ShowWindow(h_dialog,SW_SHOW);
	SetForegroundWindow(h_dialog);
	return;
}

/*
HideGUI
void __stdcall __declspec(dllexport) HideGUI(void)
This entry point is used by Winrad to tell the DLL that it has to hide its GUI, if it has one. The implementation of
this call is optional 
It has  no return value
*/
extern "C"
void LIBRTL_API  __stdcall HideGUI()
{
	DbgPrintf("HideGUI\n");
	ShowWindow(h_dialog,SW_HIDE);
	return;
}

extern "C"
void LIBRTL_API  __stdcall SwitchGUI()
{
	DbgPrintf("SwitchGUI\n");
	if (IsWindowVisible(h_dialog))
		ShowWindow(h_dialog,SW_HIDE);
	else
		ShowWindow(h_dialog,SW_SHOW);
	return;
}

/*
SetCallback
void __stdcall __declspec(dllexport) SetCallback(void (* Callback)(int, int, float,
short *))
This entry point is used by Winrad to communicate to the DLL the function address that it should invoke when a
new buffer of audio data is ready, or when an  asynchronous event must be communicated by the DLL. Of course
the new buffer of audio data is only sent by DLLs that control HW that have their own internal digitizers and do
not depend on the soundcard for input. In this case it’s up to the DLL to decide which I/O port is used to read from
the HW the digitized audio data stream. One example is the USB port. If you don’t foresee the need of an
asynchronous communication started from the DLL, simply do a return when Winrad calls this entry point.
The callback function in Winrad that the DLL is expected to call, is defined as follows :
      void extIOCallback(int cnt, int status, float IQoffs, short IQdata[])
	  Parameters :
	  cnt
	  is the number of samples returned. As the data is complex (I/Q pairs), then there are two 16 bit
	  values per sample. If negative, then the callback was called just to indicate a status change, no data
	  returned. Presently Winrad does not use this value, but rather the return value of the StartHW()
	  API, to allocate the buffers and process the audio data returned by the DLL. The cnt value is
	  checked only for negative value, meaning a status change.
	  status
	  is a status indicator (see the call GetStatus). When the DLL detects a HW change, e.g. a power On or
	  a power Off, it calls the callback function with a cnt parameter negative, indicating that no data is
	  returned, but that the call is meant just to indicate a status change.
	  Currently the status parameter has just two implemented values (apart from those used by the 
	  SDR?14/SDR?IQ hardware) :
	  100
	  This status value indicates that a sampling frequency change has taken place, either by a
	  hardware action, or by an interaction of the user with the DLL GUI.. When Winrad receives
	  this status, it calls immediately after the GetHWSR() API to know the new sampling rate. 
	  101
	  This status value indicates that a change of the LO frequency has taken place, either by a
	  hardware action, or by an interaction of the user with the DLL GUI.. When Winrad receives
	  this status, it calls immediately after the GetHWLO() API to know the new LO frequency. 
	  102
	  This status value indicates that the DLL has temporarily blocked any change to the LO
	  frequency. This may happen, e.g., when the DLL has started recording on a WAV file the
	  incoming raw data. As the center frequency has been written into the WAV file header,
	  changing it during the recording would be an error.
	  103
	  This status value indicates that changes to the LO frequency are again accepted by the DLL
	  104        ******************* CURRENTLY NOT IMPLEMENTED YET ****************************
	  This status value indicates that a change of the LO frequency has taken place, and that
	  Winrad should act so to keep the Tune frequency unchanged. When Winrad receives this
	  status, it calls immediately after the GetHWLO() API to know the new LO frequency
	  105
	  This status value indicates that a change of the Tune frequency has taken place, either by a
	  hardware action, or by an interaction of the user with the DLL GUI.. When Winrad receives
	  this status, it calls immediately after the GetTune() API to know the new Tune frequency.
	  The TuneChanged() API is not called when setting the new Tune frequency
	   
	   106
	   This status value indicates that a change of the demodulation mode has taken place, either
	   by a hardware action, or by an interaction of the user with the DLL GUI.. When Winrad
	   receives this status, it calls immediately after the GetMode() API to know the new
	   demodulation mode.  The ModeChanged() API is not called when setting the new mode.
	   107
	   This status value indicates that the DLL is asking Winrad to behave as if the user had
	   pressed the Start button. If Winrad is already started, this is equivalent to a no?op. 
	   108
	   This status value indicates that the DLL is asking Winrad to behave as if the user had
	   pressed the Stop button. If Winrad is already stopped, this is equivalent to a no?op. 
	   109
	   This status value indicates that the DLL is asking Winrad to change the passband limits
	   and/or the CW pitch. When Winrad receives this status, it calls immediately the GetFilters
	   API .
	   Upon request from the DLL writer, the status flag could be managed also for other kinds of
	   external hardware events.
	   IQoffs
	   If the external HW has the capability of determining and providing an offset value which would
	   cancel or minimize the DC offsets of the two outputs, then the DLL should set this parameter to the
	   specified value. Otherwise set it to zero.
	   IQdata
	   This is a pointer to an array of samples where the DLL is expected to place the digitized audio data
	   in interleaved format (I?Q?I?Q?I?Q etc.) in little endian ordering.   The number of bytes returned
	   must be equal to IQpairs * 2 * N, where IQpairs is the return value of the StartHW() API, and N is
	   the sizeof() of the type of data returned, as specified by the ‘type’ parameter of the InitHW() API.
*/
extern "C"
void LIBRTL_API __stdcall SetCallback(void (* myCallBack)(int, int, float, void *))
{
	DbgPrintf("SetCallback\n");
	WinradCallBack = myCallBack;
    return;
}


// DC filter
// two indipendent I/Q high pass filter to reduce DC
void DCfilterIQ(float fbuf[], int len)
{
		for (int i = 0; i < len *2; i += 2)
		{
			averageI = averageI * oneMinusRatio +(float) fbuf[i] * ratio;
			fbuf[i] -=  averageI;
			averageQ = averageQ * oneMinusRatio + (float) fbuf[i+1] * ratio;
			fbuf[i+1] -= averageQ;
		}
	
}

// Pass-through function.
// audio sample processing in front of WinradCallBack
//
int H101CallBack(void *outputBuffer, void *inputBuffer, unsigned int nBufferFrames,
	double streamTime, RtAudioStreamStatus status, void *data)
{
	int i;
	UINT8 serchar;
	float antennactrl;
	float adjgain;
	adjgain = powf(10.0f, (gainadj[attenuation_idx][ifgain_idx]) / 20.0f);
	// The number of input and output channels is equal

	//	memcpy(outputBuffer, inputBuffer, *bytes);
	//  Generate output buffer channel LEFT (antenna switch)
	
	if (antenna_idx == 0)
		antennactrl = 1.0;
	else
		antennactrl = - 1.0;

		for (i = 0; i < buffer_len; i++)
		{
			(((float *)outputBuffer)[i*2]) = antennactrl;  // left channel
			if (++idxbit > BITLEN)
			{
				idxbit = 0;
				if (uartbitcnt <= 0) // no char waiting
				{
					if (uartfifo.sampleOut(&serchar))
					{
						uartsignal = (serchar << 1) | 0x0FE00;	// data uchar da inviare
						uartbitcnt = 12;
					}
				}
				else
				{
					uartsignal = (uartsignal >> 1) | 0xFF00;
					uartbitcnt--;
				}
			}
			// processing char
			if ((uartsignal & 1) == 0)
				(((float *)outputBuffer)[ i * 2 + 1 ]) = SERIAL0;  // right channel UART 0 output
			else
				(((float *)outputBuffer)[ i * 2 + 1 ]) = SERIAL1;  // right channel UART 1 output
		}
	
		if (uartsignal == 0xffff)  // if uart is in use audio in is blanked
		{
			for (i = 0; i < buffer_len; i++)
			{
				float_buf[2 * i] = onesampledelay;
				onesampledelay = adjgain * (((float *)inputBuffer)[i * 2]);
				float_buf[2 * i + 1] = adjgain * -(((float *)inputBuffer)[i * 2 + 1]);
			}
		}
		else
			for (i = 0; i < buffer_len*2; i++)
				float_buf[i] = 0.0;


		DCfilterIQ( float_buf, buffer_len);  // remove DC

		WinradCallBack(buffer_len*2, 0, 0, (void*)float_buf); // to Winrad
	
		return 0;
}

// Dialog message processing
static INT_PTR CALLBACK MainDlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
   	switch (uMsg)
    {
        case WM_INITDIALOG:
		{	
			HICON hIcon = LoadIcon((HINSTANCE)GetWindowLong(hwndDlg, GWL_HINSTANCE),TEXT("i_config"));
			if (hIcon)
			{
				SendMessage(hwndDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
			}
			antenna_idx = H101settings.get_int(TEXT("Antenna"), TEXT("index"),0);
			ComboBox_AddString(GetDlgItem(hwndDlg, IDC_ANTENNA), antenna[0]);
			ComboBox_AddString(GetDlgItem(hwndDlg, IDC_ANTENNA), antenna[1]);
			ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_ANTENNA), antenna_idx);

			for (int i = 0; i<(sizeof(attenuation) / sizeof(attenuation[0])); i++)
			{
				ComboBox_AddString(GetDlgItem(hwndDlg, IDC_ATTENUATOR), attenuation[i].name);
			}
			attenuation_idx = H101settings.get_int(TEXT("attenuation"), TEXT("index = "), 0);
			ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_ATTENUATOR), attenuation_idx);

			for (int i = 0; i<(sizeof(ifgain) / sizeof(ifgain[0])); i++)
			{
				ComboBox_AddString(GetDlgItem(hwndDlg, IDC_IFGAIN), ifgain[i].name);
			}
			ifgain_idx = H101settings.get_int(TEXT("IF gain"), TEXT("index"), 2);
			ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_IFGAIN), ifgain_idx);

			for (unsigned int i = 0; i < soundPorts.size(); i++)
			{
				if (soundPorts[i].usb)
				{
					if (soundPorts[i].input > 0)
						ComboBox_AddString(GetDlgItem(hwndDlg, IDC_AUDIOIN), soundPorts[i].name);
					if (soundPorts[i].output > 0)
						ComboBox_AddString(GetDlgItem(hwndDlg, IDC_AUDIOOUT), soundPorts[i].name);
				}
			}
			ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_AUDIOIN), 0);
			ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_AUDIOOUT), 0);

			for (int i=0; i<(sizeof(samplerates)/sizeof(samplerates[0]));i++)
			{
				ComboBox_AddString(GetDlgItem(hwndDlg,IDC_SAMPLERATE),samplerates[i].name);
			}
			ComboBox_SetCurSel(GetDlgItem(hwndDlg,IDC_SAMPLERATE),samplerate_default);

			SendMessage(GetDlgItem(hwndDlg, IDC_PPM_S), UDM_SETRANGE, (WPARAM)TRUE, (LPARAM)MAX_PPM | (MIN_PPM << 16));

			{
				int corr;
				setEditTextProgramatically = true;
				corr = H101settings.get_int(TEXT("Freq correction"), TEXT("ppm"), 0);
				TCHAR ppm[255];
				_stprintf(ppm, TEXT("%d"), corr);
				Edit_SetText(GetDlgItem(hwndDlg, IDC_PPM), ppm);
				setEditTextProgramatically = false;
				freq_correction = 1.0 + double(corr) / double(1000000.0);
				DbgPrintf("freq correction %e\n", freq_correction);
				WinradCallBack(-1, WINRAD_LOCHANGE, 0, NULL);  // signal the real LO is different
			}
			SendMessage(GetDlgItem(hwndDlg, IDC_ADC_S), UDM_SETRANGE, (WPARAM)TRUE, (LPARAM)MAX_ADC | (MIN_ADC << 16));
			{
				adcx_default = H101settings.get_int(TEXT("ADC in volume correction"), TEXT("volx100"), 50);
				if (adcx_default > MAX_ADC) adcx_default = MAX_ADC;
				if (adcx_default < MIN_ADC) adcx_default = MIN_ADC;
				TCHAR adcx[255];
				_stprintf(adcx, TEXT("%d"), adcx_default);
				Edit_SetText(GetDlgItem(hwndDlg, IDC_ADC), adcx);
			}
			WriteIOH101();  //update
			return TRUE;
		}
        case WM_COMMAND:
            switch (GET_WM_COMMAND_ID(wParam, lParam))
            {
				case IDC_ANTENNA:
					if (GET_WM_COMMAND_CMD(wParam, lParam) == CBN_SELCHANGE)
						antenna_idx = ComboBox_GetCurSel(GetDlgItem(hwndDlg, IDC_ANTENNA));
				   		H101settings.set_int(TEXT("Antenna"), TEXT("index"), antenna_idx);
					return TRUE;

				case IDC_SAMPLERATE:
					if(GET_WM_COMMAND_CMD(wParam, lParam) == CBN_SELCHANGE)
                    { 
		//				WinradCallBack(-1,WINRAD_SRCHANGE,0,NULL);// only 48KHz
                    }
                    return TRUE;
			
				case IDC_ATTENUATOR:
					if (GET_WM_COMMAND_CMD(wParam, lParam) == CBN_SELCHANGE)
					{
						attenuation_idx = ComboBox_GetCurSel(GetDlgItem(hwndDlg, IDC_ATTENUATOR));
						WriteIOH101();
						H101settings.set_int(TEXT("attenuation"), TEXT("index"), attenuation_idx);
					}
					return TRUE;
				case IDC_IFGAIN:
					if (GET_WM_COMMAND_CMD(wParam, lParam) == CBN_SELCHANGE)
					{
						ifgain_idx = ComboBox_GetCurSel(GetDlgItem(hwndDlg, IDC_IFGAIN));
						WriteIOH101();
						H101settings.set_int(TEXT("IF gain"), TEXT("index"), ifgain_idx);
					}
					return TRUE;

				case IDC_PPM:
					if (GET_WM_COMMAND_CMD(wParam, lParam) == EN_CHANGE)
					{
						if (!setEditTextProgramatically)
						{
							TCHAR ppm[255];
							Edit_GetText((HWND)lParam, ppm, 255);
							int corr = _ttoi(ppm);
							freq_correction = 1.0 + double(corr) / double(1000000.0);
							DbgPrintf("freq correction %e\n", freq_correction);
							WinradCallBack(-1, WINRAD_LOCHANGE, 0, NULL);  // signal that the real LO is different
							H101settings.set_int(TEXT("Freq correction"), TEXT("ppm"), corr);
						}
					}
					return TRUE;
				case IDC_ADC:
					if (GET_WM_COMMAND_CMD(wParam, lParam) == EN_CHANGE)
					{
						if (!setEditTextProgramatically)
						{
							TCHAR adcx[255];
							Edit_GetText((HWND)lParam, adcx, 255);
							adcx_default = _ttoi(adcx);
							if (adcx_default > MAX_ADC) adcx_default = MAX_ADC;
							if (adcx_default < MIN_ADC) adcx_default = MIN_ADC;
							DbgPrintf("input adc correction %d /100 \n", adcx_default);
							H101settings.set_int(TEXT("ADC in volume correction"), TEXT("volx100"), adcx_default);
							if ((adacp != nullptr)&& (deviceAdcId >=0))
							{
								SetVolume(deviceAdcId, (float)adcx_default);
							}
						}
					}
					return TRUE;

				case IDC_SOUNDD:
					{
						TCHAR tstri[MAXTCHAR];
						TCHAR tstro[MAXTCHAR];
						ComboBox_GetText((GetDlgItem(h_dialog, IDC_AUDIOIN)), tstri, sizeof(tstri) / sizeof(TCHAR));
						ComboBox_GetText((GetDlgItem(h_dialog, IDC_AUDIOOUT)), tstro, sizeof(tstro) / sizeof(TCHAR));
						WinradCallBack(-1, WINRAD_STOP, 0, NULL);  // signal to stop
						StopHW();
						if ((Soundsetup(tstri) != 0) ||
						   (SoundPlaySetup(tstro) != 0))
							AdviceSoundIn();
						ShowWindow(h_dialog, SW_HIDE);
						MessageBox(NULL, TEXT("\nPlease press Stop and then Play to restart the  SDR application\n"),
							TEXT("SDR restart is required"), MB_ICONINFORMATION | MB_TOPMOST);
					}
					return TRUE;

			}
            break;

        case WM_CLOSE:
			ShowWindow(h_dialog,SW_HIDE);
            return TRUE;
			break;

		case WM_DESTROY:
			h_dialog=NULL;
            return TRUE;
			break;
    }
    return FALSE;
}



// CRC16  
void CRC16_Update(UINT16 *crc16, UINT8 *c, INT16 len)
{
	INT16 l;
	// Update the CRC
	// the CCITT 16bit algorithm (X^16 + X^12 + X^5 + 1).
	for (l = 0; l < len; l++) {
		*crc16 = (UINT8)(*crc16 >> 8) | (*crc16 << 8);
		*crc16 ^= c[l];
		*crc16 ^= (UINT8)(*crc16 & 0xff) >> 4;
		*crc16 ^= (*crc16 << 8) << 4;
		*crc16 ^= ((*crc16 & 0xff) << 4) << 1;
	}
}

void SendPacket(UINT8 *ptdata, UINT16 tdatalen)
/*
Packet trasmission to H101
*/
{
	UINT8 txch;
	UINT16 i, crc;
	// initial SYN sequence 
	WriteChar(SYN);
	WriteChar(SYN);
	WriteChar(SYN);
	WriteChar(SYN);
	/* packet init */
	WriteChar(DLE);
	WriteChar(STX);
	//
	crc = 0;
	for (i = 0; i < tdatalen; i++)
	{
		txch = ptdata[i];
		WriteChar(txch);
		CRC16_Update(&crc, &txch, 1);

		/* if DLE has been transmitted test new char,
		if it is DLE a SUB is transmitted, otherwise a DLE is added*/
		if (txch == DLE)
		{
			i++;
			if (i < tdatalen)
			{
				if (ptdata[i] == DLE)
				{
					CRC16_Update(&crc, &txch, 1);		// bug in boot ?
					txch = SUB;
				}
				else
					i--;
			}
			WriteChar(txch);
		}
	}

	/* packet end */
	WriteChar(DLE);
	WriteChar(ETX);
	/* add crc */
	txch = (crc >> 8) & 0xff;
	WriteChar(txch);
	txch = crc & 0xff;
	WriteChar(txch);
}


void PrepareAndSendPacket(unsigned char ctrl_byte, unsigned char cmd_byte, UINT8 *SubDataPacket, int nBytes)
{
	UINT8 writebuf[FRAME_SIZE]; //Transparent Data Packet
	int k;
	writebuf[0] = ctrl_byte;				//CTRL byte
	writebuf[1] = cmd_byte;					//CMD  byte
	if (nBytes) {
		for (k = 0; k<nBytes; k++)
			writebuf[2 + k] = SubDataPacket[k];	//Sub Data Packet
	}
	SendPacket(writebuf, nBytes + 2);
}

// Set dds frequency and write IO
void DDSfreq(int fdds, bool doit)
{
	if ((ddsfreq != fdds) || doit)
	{
		DWORD64 f1, ddsw;
		int i;
		UINT8 data[7];
		double fd, df, dd;
		uartfifo.clear();  // cancell previous packet in queue
		WriteIOH101();
		WriteChar(SYN);
		ddsfreq = fdds;
		fd = ((double)fdds) / 1000.0;
		dd = 429496.7296;
		df = (fd / 72000.0);	// 180 MHz /2 => 144/2 72000.0 
		f1 = (DWORD64)(df * dd * 10000);
		ddsw = 0x1000;
		ddsw = ddsw << 16;
		ddsw = f1;
		data[0] = ATM_DDS;
		for (i = 1; i <7; i++)
		{
			data[i] = (UINT8)ddsw;
			ddsw = ddsw >> 8;
		}
		PrepareAndSendPacket(CTRL_RX, AT_CMD, &data[0], 6 + 2);
		WriteChar(SYN);
	}
}
// Write IO
void WriteIOH101(void)
{
	UINT8 data[6];
	unsigned short iomant = 0x8 +
		(attenuation_idx & 0x02) +
		((attenuation_idx & 0x01) << 2);
	//       iomant |= AntennaA;

	ddsMHZ = ddsfreq / 1000000;
	if (ddsMHZ <0) ddsMHZ = 0;
	if (ddsMHZ >32) ddsMHZ = 32;
	if (ddsMHZ <1)
		iomant &= 0xf7;
	else
		iomant |= 0x08;

	unsigned short iomfiltri = tabfiltri[ddsMHZ];
	data[0] = ATM_IO;	//CMD IO
	data[1] = (iomant << 4) | ifgain_idx;
	data[2] = (iomfiltri >> 8) & 0xff;
	data[3] = iomfiltri & 0xff;

	if ((_iodata[0] != data[1]) ||   // if data change send to UART
		(_iodata[1] != data[2]) ||
		(_iodata[2] != data[3]))
	{

#ifdef _MYDEBUG
		DbgPrintf("WriteIOH101\t");
		for (int k = 0; k < 4; k++)
			DbgPrintf("%02x\t", data[k]);
		DbgPrintf("\n");
#endif
		PrepareAndSendPacket(CTRL_RX, AT_CMD, &data[0], 6 + 2);
		_iodata[0] = data[1]; // save data for reference
		_iodata[1] = data[2];
		_iodata[2] = data[3];
	}
}