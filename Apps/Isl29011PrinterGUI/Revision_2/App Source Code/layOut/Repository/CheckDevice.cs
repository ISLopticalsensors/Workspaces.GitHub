/**********************************************************************
 Module     : Repository
 Created On : 1-sep-2013      
 Class file : DeviceUtil.cs
 This class provides :
 1) The methods to communicate with the device
 2) Work as a middle layer between APPLication and harware
 
 **************************************************************************/


using IntersilLib;
using System;
using System.Windows;
using System.Windows.Controls;
using System.Timers;

namespace Inersil_WPFV2.Repository
{
    /// <summary>
    /// This class works as an interface between application and harware.
    /// </summary>
    class DeviceUtil
    {
        public static layOut.NewSensor NS = layOut.NewSensor.sensorObj;
        /// <summary>
        /// Check device is connected or not
        /// </summary>
        /// <returns></returns>
        public static HIDStatus CheckDeviceStatus(string device_path)
        {
            return HIDClass.CheckHIDConnStatus(device_path);
        }

        /// <summary>
        /// Search for the HID device.
        /// </summary>
        /// <returns>return true if HID device is detected</returns>
        public static bool SearchDevice(string device_path)
        {
            HIDStatus hidStatus = DeviceUtil.Connect(device_path);
            //if device is not fount show the retry message box
            if (hidStatus == HIDStatus.Disconnect)
            {
                //MessageBox.Show("Retry", "Retry", MessageBoxButton.YesNo);
                //return false;

                //if (NS.refreshTimer.Enabled == true)
                //{
                //    NS.refreshTimer.Stop();
                //    NS.refreshTimer.Dispose();
                //}
                return ShowRetryMsgBox();
            }
            else
            {
                return true;
            }
        }

        #region Private Methods
        private static bool ShowRetryMsgBox()
        {
            string message = "Device is not connected.\n     Want to Retry ?";
            string caption = "Retry Again";
            MessageBoxButton buttons = MessageBoxButton.YesNo;
            MessageBoxImage icon = MessageBoxImage.Information;
            if (MessageBox.Show(message, caption, buttons, icon) == MessageBoxResult.Yes)
            {
                if (DeviceUtil.Connect("") == HIDStatus.Pass)
                {
                    return true;
                }
                else
                    return ShowRetryMsgBox();
            }
            else
            {
                return false;
            }
        }

        /// <summary>
        /// Check for the HID device is connected to the system.
        /// If connected create a handle for the device
        /// </summary>
        /// <returns></returns>
        static HIDStatus Connect(string device_path)
        {
            try
            {
                return HIDClass.Connect(device_path);

            }
            catch (Exception ex) { throw ex; }
        }
        #endregion


    }
}