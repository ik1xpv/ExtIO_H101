#if !defined FREQTABH101_H
#define FREQTABH101_H
extern "C"
int getfreq(int nominal, bool mode, int* fcorr);
extern "C"
bool isclean(void);

#endif
