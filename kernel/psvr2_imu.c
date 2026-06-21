// SPDX-License-Identifier: GPL-2.0
/*
 * PSVR2 Linux driver — IMU exposed through the IIO subsystem.
 *
 * The headset reports a 6-axis IMU (accelerometer + gyroscope) at ~2 kHz. Raw
 * __s16 register values are exposed as IIO channels with an IIO_CHAN_INFO_SCALE
 * so the physical conversion stays in userspace. Axes are presented in the
 * sensor's native order; coordinate-frame remapping is left to the consumer.
 *
 * Copyright (C) 2026 PSVR2 Linux project
 */
#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/iio/kfifo_buf.h>
#include <linux/math64.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "psvr2.h"

struct psvr2_imu {
	struct iio_dev		*indio_dev;
	spinlock_t		lock;		/* protects last_* caches */
	s16			last_accel[3];
	s16			last_gyro[3];
};

/* Buffer pushed per sample; timestamp must be 8-byte aligned at the end. */
struct psvr2_imu_scan {
	s16	channels[6];	/* accel xyz, gyro xyz */
	aligned_s64 timestamp;
};

enum psvr2_imu_scan_index {
	PSVR2_SCAN_ACCEL_X,
	PSVR2_SCAN_ACCEL_Y,
	PSVR2_SCAN_ACCEL_Z,
	PSVR2_SCAN_GYRO_X,
	PSVR2_SCAN_GYRO_Y,
	PSVR2_SCAN_GYRO_Z,
	PSVR2_SCAN_TIMESTAMP,
};

#define PSVR2_ACCEL_CHAN(_axis, _idx)					\
{									\
	.type = IIO_ACCEL,						\
	.modified = 1,							\
	.channel2 = IIO_MOD_##_axis,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),			\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),		\
	.scan_index = (_idx),						\
	.scan_type = {							\
		.sign = 's', .realbits = 16, .storagebits = 16,		\
		.endianness = IIO_CPU,					\
	},								\
}

#define PSVR2_GYRO_CHAN(_axis, _idx)					\
{									\
	.type = IIO_ANGL_VEL,						\
	.modified = 1,							\
	.channel2 = IIO_MOD_##_axis,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),			\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),		\
	.scan_index = (_idx),						\
	.scan_type = {							\
		.sign = 's', .realbits = 16, .storagebits = 16,		\
		.endianness = IIO_CPU,					\
	},								\
}

static const struct iio_chan_spec psvr2_imu_channels[] = {
	PSVR2_ACCEL_CHAN(X, PSVR2_SCAN_ACCEL_X),
	PSVR2_ACCEL_CHAN(Y, PSVR2_SCAN_ACCEL_Y),
	PSVR2_ACCEL_CHAN(Z, PSVR2_SCAN_ACCEL_Z),
	PSVR2_GYRO_CHAN(X, PSVR2_SCAN_GYRO_X),
	PSVR2_GYRO_CHAN(Y, PSVR2_SCAN_GYRO_Y),
	PSVR2_GYRO_CHAN(Z, PSVR2_SCAN_GYRO_Z),
	IIO_CHAN_SOFT_TIMESTAMP(PSVR2_SCAN_TIMESTAMP),
};

static int psvr2_imu_read_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan, int *val,
			      int *val2, long mask)
{
	struct psvr2_imu *imu = iio_priv(indio_dev);
	unsigned long flags;

	switch (mask) {
	case IIO_CHAN_INFO_RAW: {
		int axis = chan->channel2 - IIO_MOD_X;

		if (axis < 0 || axis > 2)
			return -EINVAL;

		spin_lock_irqsave(&imu->lock, flags);
		if (chan->type == IIO_ACCEL)
			*val = imu->last_accel[axis];
		else
			*val = imu->last_gyro[axis];
		spin_unlock_irqrestore(&imu->lock, flags);
		return IIO_VAL_INT;
	}
	case IIO_CHAN_INFO_SCALE:
		if (chan->type == IIO_ACCEL) {
			/*
			 * m/s^2 per LSB = (4 * 9.80665) / 32767
			 *              = 3922660 / 3276700000, reduced by 20 so
			 * both terms fit in a signed int for IIO_VAL_FRACTIONAL.
			 */
			*val = 196133;
			*val2 = 163835000;
			return IIO_VAL_FRACTIONAL;
		}
		/* rad/s per LSB = (2000 deg/s * pi/180) / 32767 ~= 1.065276e-3. */
		*val = 0;
		*val2 = 1065276;
		return IIO_VAL_INT_PLUS_NANO;
	default:
		return -EINVAL;
	}
}

static const struct iio_info psvr2_imu_info = {
	.read_raw = psvr2_imu_read_raw,
};

void psvr2_imu_push(struct psvr2_device *psvr2, const s16 accel[3],
		    const s16 gyro[3], s64 timestamp_ns)
{
	struct psvr2_imu *imu = psvr2->imu;
	struct iio_dev *indio_dev;
	struct psvr2_imu_scan scan;
	unsigned long flags;

	if (!imu)
		return;
	indio_dev = imu->indio_dev;

	spin_lock_irqsave(&imu->lock, flags);
	memcpy(imu->last_accel, accel, sizeof(imu->last_accel));
	memcpy(imu->last_gyro, gyro, sizeof(imu->last_gyro));
	spin_unlock_irqrestore(&imu->lock, flags);

	if (!iio_buffer_enabled(indio_dev))
		return;

	scan.channels[PSVR2_SCAN_ACCEL_X] = accel[0];
	scan.channels[PSVR2_SCAN_ACCEL_Y] = accel[1];
	scan.channels[PSVR2_SCAN_ACCEL_Z] = accel[2];
	scan.channels[PSVR2_SCAN_GYRO_X] = gyro[0];
	scan.channels[PSVR2_SCAN_GYRO_Y] = gyro[1];
	scan.channels[PSVR2_SCAN_GYRO_Z] = gyro[2];

	iio_push_to_buffers_with_timestamp(indio_dev, &scan, timestamp_ns);
}

int psvr2_imu_register(struct psvr2_device *psvr2, struct device *parent)
{
	struct iio_dev *indio_dev;
	struct psvr2_imu *imu;
	int ret;

	indio_dev = devm_iio_device_alloc(parent, sizeof(*imu));
	if (!indio_dev)
		return -ENOMEM;

	imu = iio_priv(indio_dev);
	imu->indio_dev = indio_dev;
	spin_lock_init(&imu->lock);

	indio_dev->name = "psvr2_imu";
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &psvr2_imu_info;
	indio_dev->channels = psvr2_imu_channels;
	indio_dev->num_channels = ARRAY_SIZE(psvr2_imu_channels);

	ret = devm_iio_kfifo_buffer_setup(parent, indio_dev, NULL);
	if (ret)
		return ret;

	ret = devm_iio_device_register(parent, indio_dev);
	if (ret)
		return ret;

	psvr2->imu = imu;
	return 0;
}
