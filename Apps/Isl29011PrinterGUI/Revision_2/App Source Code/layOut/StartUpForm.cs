#define SAMPLE_OPEN_AND_READ
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Linq;
using System.Text;
using System.Windows.Forms;
using IntersilLib;
using Inersil_WPFV2.Repository;
using HidSharp;
using System.Threading;
using System.Diagnostics;
using System.Collections;


namespace layOut
{
    public partial class StartUpForm : Form
    {
        #region Global Variables
        ArrayList deviceInfo;
        public static string device_path = "";
        public StartUpForm()
        {
            InitializeComponent();
        }
        #endregion

        #region Generic Form Method
        private void StartUpForm_Load(object sender, EventArgs e)
        {
            HidDeviceLoader loader = new HidDeviceLoader();
            Thread.Sleep(2000); // Give a bit of time so our timing below is more valid as a benchmark.
            var stopwatch = new Stopwatch();
            stopwatch.Start();
            var deviceList = loader.GetDevices().ToArray();
            stopwatch.Stop();
            long deviceListTotalTime = stopwatch.ElapsedMilliseconds;
            foreach (HidDevice dev in deviceList)
            {
                deviceInfo = new ArrayList();
                deviceInfo.Add(dev);
                if (dev.DevicePath.Contains("vid_09aa&pid_2019"))
                {
                    device_path = dev.DevicePath;
                }
            }
        }
        #endregion

        #region Form Elements Click Events Methods
        private void btnConnect_Click(object sender, EventArgs e)
        {
            try
            {
                //searching the device or sensor.                
                if (DeviceUtil.SearchDevice(StartUpForm.device_path))
                {
                    GlobalVariables.Default_Slave_Address = HIDDevInfo.I2C_Slave_Address = 0x44;
                    if (GlobalVariables.MyDeviceDetected && comboDevice.SelectedItem != null)
                    {
                        if ((Application.OpenForms["SearchSensor"] as SearchSensor) != null)
                        {
                            //Form is already open
                            MessageBox.Show("Application is Already running.");
                        }
                        else
                        {
                            // Form is not open
                            //SearchSensor ss = new SearchSensor();
                            NewSensor ns = new NewSensor();
                            this.Hide();
                            ns.Show();
                        }
                    }
                }
            }
            catch (Exception ex)
            {

            }
        }
        private void btnCancel_Click(object sender, EventArgs e)
        {

            this.Close();
        }
        #endregion

    }
}