#include "Tests/LinuxSyscalls/x32/Types.h"
#include "Tests/LinuxSyscalls/x32/Ioctl/HelperDefines.h"

#include <cstdint>
#include <sound/asound.h>
#include <sys/ioctl.h>

namespace FEX::HLE::x32 {

namespace asound {
#ifndef SNDRV_TIMER_IOCTL_TREAD_OLD
#define SNDRV_TIMER_IOCTL_TREAD_OLD	_IOW('T', 0x02, int)
#endif

#ifndef SNDRV_TIMER_IOCTL_TREAD64
#define SNDRV_TIMER_IOCTL_TREAD64	_IOW('T', 0xa4, int)
#endif

#include "Tests/LinuxSyscalls/x32/Ioctl/asound.inl"
}
}
