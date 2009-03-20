
def find_kernel_driver(device):
	path="/dev/usbdev%d.%d" % (int(device.bus.dirname),int(device.usbdev.filename)+1);
	#ioctl(fd, op, arg, mutate_flag)
	#fnct.unlock()
	#bb_util.debug_object_attr(device.handle)
	#print path
	

'''
static int op_kernel_driver_active(struct libusb_device_handle *handle,
	int interface)
{
	int fd = __device_handle_priv(handle)->fd;
	struct usbfs_getdriver getdrv;
	int r;

	getdrv.interface = interface;
	r = ioctl(fd, IOCTL_USBFS_GETDRIVER, &getdrv);
	if (r) {
		if (errno == ENODATA)
			return 0;
		else if (errno == ENODEV)
			return LIBUSB_ERROR_NO_DEVICE;

		usbi_err(HANDLE_CTX(handle),
			"get driver failed error %d errno %d", r, errno);
		return LIBUSB_ERROR_OTHER;
	}

	return 1;
}
'''