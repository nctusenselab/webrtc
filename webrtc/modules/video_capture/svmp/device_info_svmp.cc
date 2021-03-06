/*
 * =====================================================================================
 *
 *       Filename:  device_info_svmp.cc
 *
 *    Description:  SVMP device info for WEBRTC framework
 *
 *        Version:  1.0
 *        Created:  06/11/2013 11:21:13 AM
 *       Revision:  none
 *
 *         Author:  Andrew Pyles (), 
 *   Organization:  
 *
 * =====================================================================================
 */

#include "device_info_svmp.h"

#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

#include <fcntl.h>
#include <linux/fb.h>
//v4l includes
#include <linux/videodev2.h>

#include "ref_count.h"
#include "trace.h"


namespace webrtc
{
namespace videocapturemodule
{
VideoCaptureModule::DeviceInfo*
VideoCaptureImpl::CreateDeviceInfo(const int32_t id)
{
    videocapturemodule::DeviceInfoSVMP *deviceInfo =
                    new videocapturemodule::DeviceInfoSVMP(id);
    if (!deviceInfo)
    {
        deviceInfo = NULL;
    }

    return deviceInfo;
}

DeviceInfoSVMP::DeviceInfoSVMP(const int32_t id)
    : DeviceInfoImpl(id)
{
}

int32_t DeviceInfoSVMP::Init()
{
    return 0;
}

DeviceInfoSVMP::~DeviceInfoSVMP()
{
}

uint32_t DeviceInfoSVMP::NumberOfDevices()
{
    WEBRTC_TRACE(webrtc::kTraceApiCall, webrtc::kTraceVideoCapture, _id, "%s", __FUNCTION__);

    uint32_t count = 0;
    char device[20];
    int fd = -1;


    /* /dev/graphics/fb0 */
    /* detect /dev/video [0-63]VideoCaptureModule entries */
    for (int n = 0; n < 64; n++)
    {
        //sprintf(device, "/dev/video%d", n);
        sprintf(device, "/dev/graphics/fb%d", n);
        if ((fd = open(device, O_RDONLY)) != -1)
        {
            close(fd);
            count++;
        }
    }

    return count;
}

int32_t DeviceInfoSVMP::GetDeviceName(
                                         uint32_t deviceNumber,
                                         char* deviceNameUTF8,
                                         uint32_t deviceNameLength,
                                         char* deviceUniqueIdUTF8,
                                         uint32_t deviceUniqueIdUTF8Length,
                                         char* /*productUniqueIdUTF8*/,
                                         uint32_t /*productUniqueIdUTF8Length*/)
{
    WEBRTC_TRACE(webrtc::kTraceApiCall, webrtc::kTraceVideoCapture, _id, "%s", __FUNCTION__);

    //memset(deviceNameUTF8, 0, deviceNameLength);
    strcpy (deviceNameUTF8, "SVMP VFB device");
    sprintf( deviceUniqueIdUTF8,"%d",1);

    // Travel through /dev/video [0-63]
    return 0; // only support one device
}

int32_t DeviceInfoSVMP::CreateCapabilityMap(
                                        const char* deviceUniqueIdUTF8)
{
    volatile int fd;
    //char device[32];
    bool found = false;

    const int32_t deviceUniqueIdUTF8Length =
                            (int32_t) strlen((char*) deviceUniqueIdUTF8);
    if (deviceUniqueIdUTF8Length > kVideoCaptureUniqueNameLength)
    {
        WEBRTC_TRACE(webrtc::kTraceError, webrtc::kTraceVideoCapture, _id, "Device name too long");
        return -1;
    }
    WEBRTC_TRACE(webrtc::kTraceInfo, webrtc::kTraceVideoCapture, _id,
               "CreateCapabilityMap called for device %s", deviceUniqueIdUTF8);

    /* detect /dev/fb [0-63] entries */
//    for (int n = 0; n < 64; ++n)
//    {
//        sprintf(device, "/dev/fb%d", n);
//        fd = open(device, O_RDONLY);
//        if (fd == -1)
//          continue;
//
//        // query device capabilities
//        struct v4l2_capability cap;
//        if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0)
//        {
//            if (cap.bus_info[0] != 0)
//            {
//                if (strncmp((const char*) cap.bus_info,
//                            (const char*) deviceUniqueIdUTF8,
//                            strlen((const char*) deviceUniqueIdUTF8)) == 0) //match with device id
//                {
//                    found = true;
//                    break; // fd matches with device unique id supplied
//                }
//            }
//            else //match for device name
//            {
//                if (IsDeviceNameMatches((const char*) cap.card,
//                                        (const char*) deviceUniqueIdUTF8))
//                {
//                    found = true;
//                    break;
//                }
//            }
//        }
//        close(fd); // close since this is not the matching device
//    }

    fd = open("/dev/graphics/fb0", O_RDWR);
    if (fd == -1){
    	 WEBRTC_TRACE(webrtc::kTraceError, webrtc::kTraceVideoCapture, _id, "Error opening FB device, errno");
    	 printf("errno: %d\n",errno);
    	 found = false;
    }else
    	 found = true;

    if (!found)
    {
        WEBRTC_TRACE(webrtc::kTraceError, webrtc::kTraceVideoCapture, _id, "no matching device found");
        return -1;
    }

    // now fd will point to the matching device
    // reset old capability map
    _captureCapabilities.clear();

    int size = FillCapabilityMap(fd);
    close(fd);

    // Store the new used device name
    _lastUsedDeviceNameLength = deviceUniqueIdUTF8Length;
    _lastUsedDeviceName = (char*) realloc(_lastUsedDeviceName,
                                                   _lastUsedDeviceNameLength + 1);
    memcpy(_lastUsedDeviceName, deviceUniqueIdUTF8, _lastUsedDeviceNameLength + 1);

    /*WEBRTC_TRACE(webrtc::kTraceInfo, webrtc::kTraceVideoCapture, _id, "CreateCapabilityMap %d",*/
               //_captureCapabilities.Size());

    return size;
}

bool DeviceInfoSVMP::IsDeviceNameMatches(const char* name,
                                          const char* deviceUniqueIdUTF8)
{
   // return true;

    if (strncmp(deviceUniqueIdUTF8, name, strlen(name)) == 0)
            return true;
    return false;
}

int32_t DeviceInfoSVMP::FillCapabilityMap(int fd)
{

    // set image format
    //struct v4l2_format video_fmt;
    struct fbdata  fb_data;
    memset(&fb_data, 0, sizeof(struct fbdata));

    //video_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    //video_fmt.fmt.pix.sizeimage = 0;

    //int totalFmts = 1;
    unsigned int videoFormats[] = { V4L2_PIX_FMT_RGB565 };
//    unsigned int videoFormats[] = {
//        V4L2_PIX_FMT_MJPEG,
//        V4L2_PIX_FMT_YUV420,
//        V4L2_PIX_FMT_YUYV };

    //int sizes = 13;
//    unsigned int size[][2] = { { 128, 96 }, { 160, 120 }, { 176, 144 },
//                               { 320, 240 }, { 352, 288 }, { 640, 480 },
//                               { 704, 576 }, { 800, 600 }, { 960, 720 },
//                               { 1280, 720 }, { 1024, 768 }, { 1440, 1080 },
//                               { 1920, 1080 } };
    if(ioctl(fd, FBIOGET_VSCREENINFO, &fb_data.var) >=0) {
	fb_data.len = fb_data.var.xres_virtual * fb_data.var.yres_virtual * ( fb_data.var.bits_per_pixel / 8 );
	//fb_data.base = (unsigned char*)mmap(NULL,fb_data.len , PROT_READ, MAP_SHARED, fd, 0);
//	if(fb_data.base == MAP_FAILED) {
//		//printf("Cannot MMAP framebuffer  %s\n",Strerror_r(errno,errbuf,256));
//		WEBRTC_TRACE(webrtc::kTraceError, webrtc::kTraceVideoCapture, _id, "mmap error");
//		//close(fd);
//		//free(f);
//		return -1;
//	}
    }else {
	    return -1;

    }
    VideoCaptureCapability cap;
    cap.width = fb_data.var.xres;
    cap.height = fb_data.var.yres;
    cap.expectedCaptureDelay = 30;
    int fmts = 0;
    if (videoFormats[fmts] == V4L2_PIX_FMT_YUYV)
    {
	    cap.rawType = kVideoYUY2;
    }
    else if (videoFormats[fmts] == V4L2_PIX_FMT_MJPEG)
    {
	    cap.rawType = kVideoMJPEG;
    }

    else if (videoFormats[fmts] == V4L2_PIX_FMT_RGB24)
    {
	    cap.rawType = kVideoRGB24;
    }
    else if (videoFormats[fmts] == V4L2_PIX_FMT_RGB565)
    {
    	cap.rawType = kVideoRGB565;
    }
    if(cap.width >= 800 && cap.rawType != kVideoMJPEG)
    {
	    cap.maxFPS = 15;
    }
    else
    {
	    cap.maxFPS = 30;
    }
    //int index = 0;

    _captureCapabilities.push_back(cap);
    //index++;
    WEBRTC_TRACE(webrtc::kTraceInfo, webrtc::kTraceVideoCapture, _id,
		    "Camera capability, width:%d height:%d type:%d fps:%d",
		    cap.width, cap.height, cap.rawType, cap.maxFPS);

    WEBRTC_TRACE(webrtc::kTraceInfo, webrtc::kTraceVideoCapture, _id, "CreateCapabilityMap %d",
	       _captureCapabilities.size());
    return _captureCapabilities.size();
    //    int index = 0;
//    for (int fmts = 0; fmts < totalFmts; fmts++)
//    {
//        for (int i = 0; i < sizes; i++)
//        {
//            video_fmt.fmt.pix.pixelformat = videoFormats[fmts];
//            video_fmt.fmt.pix.width = size[i][0];
//            video_fmt.fmt.pix.height = size[i][1];
//
//            //if (ioctl(fd, VIDIOC_TRY_FMT, &video_fmt) >= 0)
//            {
//                if ((video_fmt.fmt.pix.width == size[i][0])
//                    && (video_fmt.fmt.pix.height == size[i][1]))
//                {
//                    VideoCaptureCapability *cap = new VideoCaptureCapability();
//                    cap->width = video_fmt.fmt.pix.width;
//                    cap->height = video_fmt.fmt.pix.height;
//                    cap->expectedCaptureDelay = 120;
//                    if (videoFormats[fmts] == V4L2_PIX_FMT_YUYV)
//                    {
//                        cap->rawType = kVideoYUY2;
//                    }
//                    else if (videoFormats[fmts] == V4L2_PIX_FMT_MJPEG)
//                    {
//                        cap->rawType = kVideoMJPEG;
//                    }
//                    else if (videoFormats[fmts] == V4L2_PIX_FMT_RGB24)
//		    {
//                        cap->rawType = kVideoRGB24;
//		    }
//
//                    // get fps of current camera mode
//                    // V4l2 does not have a stable method of knowing so we just guess.
//                    if(cap->width >= 800 && cap->rawType != kVideoMJPEG)
//                    {
//                        cap->maxFPS = 15;
//                    }
//                    else
//                    {
//                        cap->maxFPS = 30;
//                    }
//
//                    _captureCapabilities.Insert(index, cap);
//                    index++;
//                    WEBRTC_TRACE(webrtc::kTraceInfo, webrtc::kTraceVideoCapture, _id,
//                               "Camera capability, width:%d height:%d type:%d fps:%d",
//                               cap->width, cap->height, cap->rawType, cap->maxFPS);
//                }
//            }
//        }
//    }

    /*WEBRTC_TRACE(webrtc::kTraceInfo, webrtc::kTraceVideoCapture, _id, "CreateCapabilityMap %d",*/
               /*_captureCapabilities.Size());*/
    return _captureCapabilities.size();
}

} // namespace videocapturemodule
} // namespace webrtc
