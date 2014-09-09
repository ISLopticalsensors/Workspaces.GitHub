/* File         : ProximitySensor.cpp
 * Ver          : 1.0
 *
 * Copyright (C) 2008 The Android Open Source Project
 *
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <fcntl.h>
#include <errno.h>
#include <math.h>
#include <poll.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/select.h>

#include <cutils/log.h>

#include "ProximitySensor.h"
#include "sensors.h"

#define EVENT_TYPE_PROXIMITY		ABS_DISTANCE

#define PROXIMITY_THRESHOLD			5.0f
/*****************************************************************************/

enum input_device_name {
	SENSOR_DEVICE,
	SUPPORTED_PSENSOR_COUNT,
};

static const char *data_device_name[] = {
	[SENSOR_DEVICE] = DEVICE_NAME,			/* must be same as in register as sensor driver name */

};

static const char *input_sysfs_path_list[] = {
	[SENSOR_DEVICE]   = "/sys/class/input/%s/device/"	/* Event generated path regarding sensor driver */
};

static const char *input_sysfs_enable_list[] = {
	[SENSOR_DEVICE]   = "enable"
};


	ProximitySensor::ProximitySensor()
: SensorBase(NULL, NULL),
	mEnabled(0),
	mInputReader(4),
	mHasPendingEvent(false),
	sensor_index(-1)
{
	int i;
	mPendingEvent.version = sizeof(sensors_event_t);
	mPendingEvent.sensor = SENSORS_PROXIMITY_HANDLE;
	mPendingEvent.type = SENSOR_TYPE_PROXIMITY;
	memset(mPendingEvent.data, 0, sizeof(mPendingEvent.data));

	for(i = 0; i < SUPPORTED_PSENSOR_COUNT; i++) {
		data_name = data_device_name[i];

		// data_fd is not initialized if data_name passed
		// to SensorBase is NULL.
		data_fd = openInput(data_name);
		if (data_fd > 0) {
			sensor_index = i;
			break;
		}
	}

	if (data_fd) {
		snprintf(input_sysfs_path, sizeof(input_sysfs_path),
				input_sysfs_path_list[i], input_name);
		input_sysfs_path_len = strlen(input_sysfs_path);
		enable(0, 1);
	}
}

ProximitySensor::~ProximitySensor() {
	if (mEnabled) {
		enable(0, 0);
	}
}

int ProximitySensor::setInitialState() {
	struct input_absinfo absinfo;
	if (!ioctl(data_fd, EVIOCGABS(EVENT_TYPE_PROXIMITY), &absinfo)) {
		// make sure to report an event immediately
		mHasPendingEvent = true;
		mPendingEvent.distance = indexToValue(absinfo.value);
	}
	return 0;
}

int ProximitySensor::enable(int32_t handle, int en) {
	int flags = en ? 1 : 0;
	if (flags != mEnabled) {
		int fd;
		if (sensor_index >= 0) {
			strlcpy(&input_sysfs_path[input_sysfs_path_len], input_sysfs_enable_list[sensor_index],
					sizeof(input_sysfs_path) - input_sysfs_path_len);
		} else
			return -1;

		/* sysfs path from where we can enable sensor device driver */
		fd = open(PROX_ENABLE_SYSPATH, O_RDWR);

		if (fd >= 0) {
			char buf[2];
			buf[1] = 0;
			if (flags) {
				buf[0] = '1';
			} else {
				buf[0] = '0';
			}
			write(fd, buf, sizeof(buf));
			close(fd);
			mEnabled = flags;
			setInitialState();
			return 0;
		}
		return -1;
	}
	return 0;
}

bool ProximitySensor::hasPendingEvents() const {
	return mHasPendingEvent;
}

int ProximitySensor::readEvents(sensors_event_t* data, int count)
{
	if (count < 1)
		return -EINVAL;

	if (mHasPendingEvent) {
		mHasPendingEvent = false;
		mPendingEvent.timestamp = getTimestamp();
		*data = mPendingEvent;
		return mEnabled ? 1 : 0;
	}

	ssize_t n = mInputReader.fill(data_fd);
	if (n < 0)
		return n;

	int numEventReceived = 0;
	input_event const* event;

	while (count && mInputReader.readEvent(&event)) {
		int type = event->type;
		if (type == EV_ABS) {
			if (event->code == EVENT_TYPE_PROXIMITY) {
				if (event->value != -1) {
					// FIXME: not sure why we're getting -1 sometimes
					mPendingEvent.distance = indexToValue(event->value);
				}
			}
		} else if (type == EV_SYN) {
			mPendingEvent.timestamp = timevalToNano(event->time);
			if (mEnabled) {
				*data++ = mPendingEvent;
				count--;
				numEventReceived++;
			}
		} else {
			ALOGE("LightSensor: unknown event (type=%d, code=%d)",
			type, event->code);
		}
		mInputReader.next();
	}
	return numEventReceived;
}

float ProximitySensor::indexToValue(size_t index) const
{
	return index * PROXIMITY_THRESHOLD;
}
