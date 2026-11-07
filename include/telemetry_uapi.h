#ifndef TELEMETRY_UAPI_H
#define TELEMETRY_UAPI_H

#include <linux/types.h>
#include <linux/ioctl.h>

struct telemetry_sample {
	__u64 ts_ns;
	__u32 seq;
	__s32 temp_mc;
	__u32 pressure_pa;
	__u32 reserved;
};

struct telemetry_stats {
	__u64 produced;
	__u64 consumed;
	__u64 dropped;
	__u32 high_watermark;
	__u32 rate_hz;
	__u32 buf_samples;
	__u32 reserved;
};

#define TELEM_IOC_MAGIC     'T'
#define TELEM_IOC_SET_RATE  _IOW(TELEM_IOC_MAGIC, 1, __u32)
#define TELEM_IOC_GET_STATS _IOR(TELEM_IOC_MAGIC, 2, struct telemetry_stats)
#define TELEM_IOC_RESET     _IO(TELEM_IOC_MAGIC, 3)

_Static_assert(sizeof(struct telemetry_sample) == 24, "sample must be 24 bytes");

#endif
