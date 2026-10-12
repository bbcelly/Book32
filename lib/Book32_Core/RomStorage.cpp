#include "RomStorage.h"
#if defined(BOARD_LILYGO_T5S3_PRO) || defined(BOARD_SEEED_STICKY)
std::atomic<int> RomStorage::owner{RomStorage::Idle};
#endif
