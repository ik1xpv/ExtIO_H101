
#include <Windows.h>
#include "SoundUti.h"

/*
	Pick the Device with idx in the device list eRender, eCapture
*/
bool PickDevice(int idx, IMMDevice **DeviceToUse)
{
	HRESULT hr;
	bool retValue = true;
	IMMDeviceEnumerator *deviceEnumerator = NULL;
	IMMDeviceCollection *erenderCollection = NULL;
	IMMDeviceCollection *ecaptureCollection = NULL;

	hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&deviceEnumerator));
	if (FAILED(hr))
	{
		DbgPrintf("Unable to instantiate device enumerator: %x\n", hr);
		retValue = false;
		goto Exit;
	}

	IMMDevice *device = NULL;

	//
	//  First off, if none of the console switches was specified, use the console device.
	//

	//
	//  The user didn't specify an output device, prompt the user for a device and use that.
	//
	hr = deviceEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &erenderCollection);
	if (FAILED(hr))
	{
		DbgPrintf("Unable to retrieve device collection: %x\n", hr);
		retValue = false;
		goto Exit;
	}

	hr = deviceEnumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &ecaptureCollection);
	if (FAILED(hr))
	{
		DbgPrintf("Unable to retrieve device collection: %x\n", hr);
		retValue = false;
		goto Exit;
	}
	UINT erenderdeviceCount;
	UINT ecapturedeviceCount;

	long deviceIndex;
	deviceIndex = idx;

	hr = erenderCollection->GetCount(&erenderdeviceCount);
	printf("Count erender : %d\n", erenderdeviceCount);
	if (FAILED(hr))
	{
		DbgPrintf("Unable to get device collection length: %x\n", hr);
		retValue = false;
		goto Exit;
	}

	hr = ecaptureCollection->GetCount(&ecapturedeviceCount);
	printf("Count capture : %d\n", ecapturedeviceCount);
	if (FAILED(hr))
	{
		DbgPrintf("Unable to get device collection length: %x\n", hr);
		retValue = false;
		goto Exit;
	}

	//		LPWSTR deviceName = TEXT("test ");
	if (deviceIndex < (long)erenderdeviceCount)
	{
		hr = erenderCollection->Item(deviceIndex, &device);
		if (FAILED(hr))
		{
			DbgPrintf("Unable to retrieve device %d: %x\n", deviceIndex, hr);
			retValue = false;
			goto Exit;
		}
	}
	else
	{
		hr = ecaptureCollection->Item(deviceIndex - erenderdeviceCount, &device);
		if (FAILED(hr))
		{
			DbgPrintf("Unable to retrieve device %d: %x\n", deviceIndex, hr);
			retValue = false;
			goto Exit;
		}
	}




	*DeviceToUse = device;
	retValue = true;
Exit:
	SafeRelease(&erenderCollection);
	SafeRelease(&ecaptureCollection);
	SafeRelease(&deviceEnumerator);

	return retValue;
}


int SetVolume(UINT idx, float valuex100)
{
	int result = 0;
	IMMDevice *device = NULL;
	HRESULT hr;
	hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
	if (FAILED(hr))
	{
		DbgPrintf("Unable to initialize COM: %x\n", hr);
		result = hr;
		goto Exit;
	}
	if (!PickDevice(idx, &device))
	{
		result = -1;
		goto Exit;
	}
	IAudioEndpointVolume *endpointVolume;
	hr = device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, NULL, reinterpret_cast<void **>(&endpointVolume));
	if (FAILED(hr))
	{
		DbgPrintf("Unable to activate endpoint volume on output device: %x\n", hr);
		result = -1;
		goto Exit;
	}
	BOOL currentMute = FALSE;
	hr = endpointVolume->SetMute(currentMute, NULL);
	if (FAILED(hr))
	{
		DbgPrintf("Unable to set mute state: %x\n", hr);
		result = -1;
		goto Exit;
	}
	hr = endpointVolume->SetMasterVolumeLevelScalar(valuex100 / 100.0f, NULL);
	if (FAILED(hr))
	{
		DbgPrintf("Unable to decrease volume: %x\n", hr);
		result = -1;
		goto Exit;
	}
	UINT currentStep, stepCount;
	hr = endpointVolume->GetVolumeStepInfo(&currentStep, &stepCount);
	if (FAILED(hr))
	{
		DbgPrintf("Unable to get current volume step: %x\n", hr);
		result = -1;
		goto Exit;
	}
	float currentVolume;
	hr = endpointVolume->GetMasterVolumeLevelScalar(&currentVolume);
	if (FAILED(hr))
	{
		DbgPrintf("Unable to get current volume step: %x\n", hr);
		result = -1;
		goto Exit;
	}
	hr = endpointVolume->GetMute(&currentMute);
	if (FAILED(hr))
	{
		DbgPrintf("Unable to retrieve current mute state: %x\n", hr);
		result = -1;
		goto Exit;
	}
	DbgPrintf("Current master volume: %f.   Step %d of step range 0-%d.\nEndpoint Mute: %S\n", currentVolume, currentStep, stepCount, currentMute ? L"Muted" : L"Unmuted");
Exit:
	SafeRelease(&device);
	CoUninitialize();
	return result;
}

static HWND hwndTarget = NULL;
static TCHAR classSearch[MAXTCHAR];
static TCHAR textSearchEng[MAXTCHAR];
static TCHAR textSearchIta[MAXTCHAR];


BOOL CALLBACK DoClassSearchIter(HWND hwnd, LPARAM lParam)
{
	TCHAR Name[MAXTCHAR];
	TCHAR text[MAXTCHAR];
	GetClassName(hwnd, Name, MAXTCHAR);
	if (_tcscmp(Name, classSearch) == 0)
	{
		if (lParam == 0)
		{
			// class found
			hwndTarget = hwnd;
			return FALSE;
		}

		if (_tcscmp(Name, TEXT("ComboBox")) == 0)
		{
			SendMessage(hwnd, CB_GETLBTEXT, 0, (LPARAM)text);
			if ((_tcsstr(text, textSearchIta) != NULL) ||
				(_tcsstr(text, textSearchEng) != NULL))
				hwndTarget = hwnd;
			return FALSE;
		}

		TCHAR text[256];
		GetWindowText(hwnd, text, MAXTCHAR);
		_tprintf(TEXT("%s\n"), text);
		if ((_tcsstr(text, textSearchIta) != NULL) ||
			(_tcsstr(text, textSearchEng) != NULL))
		{
			hwndTarget = hwnd;
			return FALSE;
		}


	}
	return TRUE;
}


HWND DoContrlSearchTree(HWND hwndRoot, int mode)
{
	hwndTarget = NULL;
	// now do it to all the descendants (children, grandchildren, etc.)
	EnumChildWindows(hwndRoot, DoClassSearchIter, (LPARAM)mode);
	return hwndTarget;
}
HWND DoContrlSearchTree(HWND hwndRoot, TCHAR * classname)
{
	_tcscpy_s(classSearch, classname);
	return DoContrlSearchTree(hwndRoot, 0);
}

HWND DoControlTextSearchTree(HWND hwndRoot, TCHAR * classname, TCHAR * textnameENG, TCHAR * textnameITA)
{
	_tcscpy_s(classSearch, classname);
	_tcscpy_s(textSearchEng, textnameENG);
	_tcscpy_s(textSearchIta, textnameITA);
	return DoContrlSearchTree(hwndRoot, 1);
}

// close audio config windows with Eng and Ita caption
void CloseSoundConfig()
{
	HWND hw;
	hw = FindWindow(NULL, TEXT("Proprietà - Microphone"));
	if (hw != NULL) CloseWindow(hw);
	hw = FindWindow(NULL, TEXT("Proprietà - Microfono"));
	if (hw != NULL) CloseWindow(hw);
	hw = FindWindow(NULL, TEXT("Microphone properties"));
	if (hw != NULL) CloseWindow(hw);
	hw = FindWindow(NULL, TEXT("Proprietà - Speakers"));
	if (hw != NULL) CloseWindow(hw);
	hw = FindWindow(NULL, TEXT("Proprietà - Altoparlanti"));
	if (hw != NULL) CloseWindow(hw);
	hw = FindWindow(NULL, TEXT("Speakers properties"));
	if (hw != NULL) CloseWindow(hw);
	hw = FindWindow(NULL, TEXT("Audio"));
	if (hw != NULL) CloseWindow(hw);
	hw = FindWindow(NULL, TEXT("Sound"));
	if (hw != NULL) CloseWindow(hw);
}



int Soundsetup(TCHAR * name)
{

	HWND haudio = NULL;
	HWND hlistview = NULL;
	HWND hpropButton = NULL;
	HWND hRecDevice = NULL;
	HWND hTabCtrl = NULL;
	HWND hTabCombo = NULL;

	CloseSoundConfig();
	// Run mmsys.cpl
	int nRet = (int)ShellExecute(0, TEXT("open"), TEXT("rundll32.exe"), TEXT("shell32.dll,Control_RunDLL mmsys.cpl,,1"), 0, SW_SHOWNORMAL);
	if (nRet <= 32) {
		DWORD dw = GetLastError();
		TCHAR szMsg[MAXTCHAR];
		FormatMessage(
			FORMAT_MESSAGE_FROM_SYSTEM,
			0, dw, 0,
			szMsg, sizeof(szMsg),
			NULL
			);
		MessageBox(NULL, szMsg, TEXT("Error launching mmsys.cpl"), MB_ICONINFORMATION);
		return -1;
	}
	// look for Sound window
	int retry = 20;
	while (retry > 0)
	{
		Sleep(100);
		haudio = FindWindow(NULL, TEXT("Sound"));
		if (haudio != NULL)  break;
		haudio = FindWindow(NULL, TEXT("Audio"));
		if (haudio != NULL) break;
		retry--;
	}
	if (haudio == NULL) return -2; // no audio program
	Sleep(500);
	hlistview = DoContrlSearchTree(haudio, TEXT("SysListView32"));  // search for handle
	if (hlistview == NULL) return -3; // no audio program


	if (((_tcsstr(name, TEXT("Microphone")) == NULL) && (_tcsstr(name, TEXT("Microfono")) == NULL))||
		(_tcsstr(name, TEXT("USB Audio CODEC")) == NULL)) 
		return -8;

	int count = (int)SendMessage(hlistview, LVM_GETITEMCOUNT, 0, 0);
	int i;
	int mikedevice = -1;
	LVITEM lvi, *_lvi;
	TCHAR item[512], subitem[512];
	TCHAR *_item, *_subitem;
	unsigned long pid;
	HANDLE process;

	GetWindowThreadProcessId(hlistview, &pid);
	process = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION, FALSE, pid);

	_lvi = (LVITEM*)VirtualAllocEx(process, NULL, sizeof(LVITEM), MEM_COMMIT, PAGE_READWRITE);
	_item = (TCHAR*)VirtualAllocEx(process, NULL, 512, MEM_COMMIT, PAGE_READWRITE);
	_subitem = (TCHAR*)VirtualAllocEx(process, NULL, 512, MEM_COMMIT, PAGE_READWRITE);
	lvi.cchTextMax = 512;

	for (i = 0; i<count; i++)
	{
		lvi.iSubItem = 0;
		lvi.pszText = _item;
		WriteProcessMemory(process, _lvi, &lvi, sizeof(LVITEM), NULL);
		SendMessage(hlistview, LVM_GETITEMTEXT, (WPARAM)i, (LPARAM)_lvi);

		lvi.iSubItem = 1;
		lvi.pszText = _subitem;
		WriteProcessMemory(process, _lvi, &lvi, sizeof(LVITEM), NULL);
		SendMessage(hlistview, LVM_GETITEMTEXT, (WPARAM)i, (LPARAM)_lvi);
		ReadProcessMemory(process, _item, item, 512, NULL);
		ReadProcessMemory(process, _subitem, subitem, 512, NULL);

		if (((_tcsstr(TEXT("Microphone  "), item) != NULL) || (_tcsstr(TEXT("Microfono  "), item) != NULL)) &&
			(_tcsstr(TEXT("USB Audio CODEC  "), subitem) != NULL))
			mikedevice = i;
		_tprintf(TEXT("%s  %s\n"), item, subitem);
	}

	//	ListView_SetItemState(hlistview, 0, LVIS_FOCUSED | LVIS_SELECTED, 0x000F); // select device
	if (mikedevice >= 0)
	{
		lvi.state = LVIS_SELECTED | LVIS_FOCUSED;
		lvi.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
		WriteProcessMemory(process, _lvi, &lvi, sizeof(LVITEM), NULL);
		SendMessage(hlistview, LVM_SETITEMSTATE, (WPARAM)mikedevice, (LPARAM)_lvi);
	}

	VirtualFreeEx(process, _lvi, 0, MEM_RELEASE);
	VirtualFreeEx(process, _item, 0, MEM_RELEASE);
	VirtualFreeEx(process, _subitem, 0, MEM_RELEASE);

	if (mikedevice < 0) return -4; // input device not found

	hpropButton = DoControlTextSearchTree(haudio, TEXT("Button"), TEXT("Properties"), TEXT("Proprietà"));


	if (hpropButton)
		PostMessage(hpropButton, BM_CLICK, (WPARAM)0, (LPARAM)0); // activate the properties window
	else
		return -5;
	Sleep(500);
	retry = 20;
	while (retry > 0)
	{
		Sleep(300);
		hRecDevice = FindWindow(NULL, TEXT("Proprietà - Microphone"));
		if (hRecDevice != NULL)  break;
		hRecDevice = FindWindow(NULL, TEXT("Proprietà - Microfono"));
		if (hRecDevice != NULL)  break;
		hRecDevice = FindWindow(NULL, TEXT("Microphone properties"));
		if (hRecDevice != NULL)  break;
		retry--;
	}

	hTabCtrl = DoContrlSearchTree(hRecDevice, TEXT("SysTabControl32"));  // search for handle

	unsigned long pid2;
	HANDLE process2;
	if (hTabCtrl)
	{
		GetWindowThreadProcessId(hTabCtrl, &pid2);
		process2 = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION, FALSE, pid2);

		int ntab = TabCtrl_GetItemCount(hTabCtrl);
		_tprintf(TEXT("Tab n  %d\n"), ntab);
		TabCtrl_SetCurFocus(hTabCtrl, ntab - 1);

		hTabCombo = DoControlTextSearchTree(hRecDevice, TEXT("ComboBox"), TEXT("Channels"), TEXT("Canali"));

		TCHAR text[256];
		SendMessage(hTabCombo, CB_GETLBTEXT, 0, (LPARAM)text);
		_tprintf(TEXT("\nCombo %x /t :%s"), hTabCombo, text);
		int idx = SendMessage(hTabCombo, CB_GETCOUNT, (WPARAM)0, (LPARAM)0);
		_tprintf(TEXT("\nCombo count = %d "), idx);

		SendMessage(hTabCombo, CB_SHOWDROPDOWN, (WPARAM)TRUE, (LPARAM)0);
		int xdx = SendMessage(hTabCombo, CB_GETCURSEL, (WPARAM)0, (LPARAM)0);
		//     Change the combo box to the value higher value
		if (xdx != (idx - 1))
		{
			SendMessage(hTabCombo, CB_SETCURSEL, (WPARAM)idx - 1, (LPARAM)0);
			SendMessage(GetParent(hTabCombo), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hTabCombo), CBN_SELENDOK), (LPARAM)(hTabCombo));
			Sleep(1000);
			SendMessage(hRecDevice, WM_COMMAND, 1, NULL);
			SendMessage(haudio, WM_COMMAND, 1, NULL);
		}
	}
	else
		return -6;
	Sleep(500);
	// close the Sound windows    
	PostMessage(hRecDevice, WM_CLOSE, 0, 0);
	PostMessage(haudio, WM_CLOSE, 0, 0);
	return 0;


}


int SoundPlaySetup(TCHAR * name)
{

	HWND haudio = NULL;
	HWND hlistview = NULL;
	HWND hpropButton = NULL;
	HWND hRecDevice = NULL;
	HWND hTabCtrl = NULL;
	HWND hTabCombo = NULL;

	CloseSoundConfig();
	// Run mmsys.cpl
	int nRet = (int)ShellExecute(0, TEXT("open"), TEXT("rundll32.exe"), TEXT("shell32.dll,Control_RunDLL mmsys.cpl,,0"), 0, SW_SHOWNORMAL);
	if (nRet <= 32) {
		DWORD dw = GetLastError();
		TCHAR szMsg[MAXTCHAR];
		FormatMessage(
			FORMAT_MESSAGE_FROM_SYSTEM,
			0, dw, 0,
			szMsg, sizeof(szMsg),
			NULL
			);
		MessageBox(NULL, szMsg, TEXT("Error launching mmsys.cpl"), MB_ICONINFORMATION);
		return -1;
	}
	// look for Sound window
	int retry = 20;
	while (retry > 0)
	{
		Sleep(100);
		haudio = FindWindow(NULL, TEXT("Sound"));
		if (haudio != NULL)  break;
		haudio = FindWindow(NULL, TEXT("Audio"));
		if (haudio != NULL) break;
		retry--;
	}
	if (haudio == NULL) return -2; // no audio program
	Sleep(500);
	hlistview = DoContrlSearchTree(haudio, TEXT("SysListView32"));  // search for handle
	if (hlistview == NULL) return -3; // no audio program


	if (((_tcsstr(name, TEXT("Speakers")) == NULL) && (_tcsstr(name, TEXT("Altoparlanti")) == NULL)) ||
		(_tcsstr(name, TEXT("USB Audio CODEC")) == NULL))
		return -8;

	int count = (int)SendMessage(hlistview, LVM_GETITEMCOUNT, 0, 0);
	int i;
	int spkrsdevice = -1;
	LVITEM lvi, *_lvi;
	TCHAR item[512], subitem[512];
	TCHAR *_item, *_subitem;
	unsigned long pid;
	HANDLE process;

	GetWindowThreadProcessId(hlistview, &pid);
	process = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION, FALSE, pid);

	_lvi = (LVITEM*)VirtualAllocEx(process, NULL, sizeof(LVITEM), MEM_COMMIT, PAGE_READWRITE);
	_item = (TCHAR*)VirtualAllocEx(process, NULL, 512, MEM_COMMIT, PAGE_READWRITE);
	_subitem = (TCHAR*)VirtualAllocEx(process, NULL, 512, MEM_COMMIT, PAGE_READWRITE);
	lvi.cchTextMax = 512;

	for (i = 0; i < count; i++)
	{
		lvi.iSubItem = 0;
		lvi.pszText = _item;
		WriteProcessMemory(process, _lvi, &lvi, sizeof(LVITEM), NULL);
		SendMessage(hlistview, LVM_GETITEMTEXT, (WPARAM)i, (LPARAM)_lvi);

		lvi.iSubItem = 1;
		lvi.pszText = _subitem;
		WriteProcessMemory(process, _lvi, &lvi, sizeof(LVITEM), NULL);
		SendMessage(hlistview, LVM_GETITEMTEXT, (WPARAM)i, (LPARAM)_lvi);
		ReadProcessMemory(process, _item, item, 512, NULL);
		ReadProcessMemory(process, _subitem, subitem, 512, NULL);

		if (((_tcsstr(TEXT("Speakers  "), item) != NULL) || (_tcsstr(TEXT("Altoparlanti  "), item) != NULL)) &&
			(_tcsstr(TEXT("USB Audio CODEC  "), subitem) != NULL))
			{
			spkrsdevice = i;
			_tprintf(TEXT("%s  %s\n"), item, subitem);
			}
	}

	//	ListView_SetItemState(hlistview, 0, LVIS_FOCUSED | LVIS_SELECTED, 0x000F); // select device
	if (spkrsdevice >= 0)
	{
		lvi.state = LVIS_SELECTED | LVIS_FOCUSED;
		lvi.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
		WriteProcessMemory(process, _lvi, &lvi, sizeof(LVITEM), NULL);
		SendMessage(hlistview, LVM_SETITEMSTATE, (WPARAM)spkrsdevice, (LPARAM)_lvi);
	}

	VirtualFreeEx(process, _lvi, 0, MEM_RELEASE);
	VirtualFreeEx(process, _item, 0, MEM_RELEASE);
	VirtualFreeEx(process, _subitem, 0, MEM_RELEASE);

	if (spkrsdevice < 0) 
		return -4; //  device not found

	hpropButton = DoControlTextSearchTree(haudio, TEXT("Button"), TEXT("Properties"), TEXT("Proprietà"));


	if (hpropButton)
		PostMessage(hpropButton, BM_CLICK, (WPARAM)0, (LPARAM)0); // activate the properties window
	else
		return -5;
	Sleep(500);
	retry = 20;
	while (retry > 0)
	{
		Sleep(300);
		hRecDevice = FindWindow(NULL, TEXT("Proprietà - Speakers"));
		if (hRecDevice != NULL)  break;
		hRecDevice = FindWindow(NULL, TEXT("Proprietà - Altoparlanti"));
		if (hRecDevice != NULL)  break;
		hRecDevice = FindWindow(NULL, TEXT("Speakers properties"));
		if (hRecDevice != NULL)  break;
		retry--;
	}

	hTabCtrl = DoContrlSearchTree(hRecDevice, TEXT("SysTabControl32"));  // search for handle

	unsigned long pid2;
	HANDLE process2;
	if (hTabCtrl)
	{
		GetWindowThreadProcessId(hTabCtrl, &pid2);
		process2 = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION, FALSE, pid2);

		int ntab = TabCtrl_GetItemCount(hTabCtrl);
		_tprintf(TEXT("Tab n  %d\n"), ntab);
		TabCtrl_SetCurFocus(hTabCtrl, ntab - 1);

		hTabCombo = DoControlTextSearchTree(hRecDevice, TEXT("ComboBox"), TEXT("16 bit"), TEXT("16 bit"));
		if (hTabCombo == NULL) 
			return -9;
		TCHAR text[256];
		SendMessage(hTabCombo, CB_GETLBTEXT, 0, (LPARAM)text);
		_tprintf(TEXT("\nCombo %x /t :%s"), hTabCombo, text);
		int idx = SendMessage(hTabCombo, CB_GETCOUNT, (WPARAM)0, (LPARAM)0);
		_tprintf(TEXT("\nCombo count = %d "), idx);

		SendMessage(hTabCombo, CB_SHOWDROPDOWN, (WPARAM)TRUE, (LPARAM)0);
		int xdx = SendMessage(hTabCombo, CB_GETCURSEL, (WPARAM)0, (LPARAM)0);
		//     Change the combo box to the value higher value
		if (xdx != (idx - 1))
		{
			SendMessage(hTabCombo, CB_SETCURSEL, (WPARAM)idx - 1, (LPARAM)0);
			SendMessage(GetParent(hTabCombo), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hTabCombo), CBN_SELENDOK), (LPARAM)(hTabCombo));
			Sleep(1000);
			SendMessage(hRecDevice, WM_COMMAND, 1, NULL);
			SendMessage(haudio, WM_COMMAND, 1, NULL);
		}
	}
	else
		return -6;
	Sleep(500);
	// close the Sound windows    
	PostMessage(hRecDevice, WM_CLOSE, 0, 0);
	PostMessage(haudio, WM_CLOSE, 0, 0);
	return 0;


}
