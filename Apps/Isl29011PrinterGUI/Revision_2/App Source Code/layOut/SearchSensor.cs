using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Linq;
using System.Text;
using System.Windows.Forms;
using IntersilLib;
using System.Collections;

namespace layOut
{
    public partial class SearchSensor : Form
    {
        byte[] CopyReg = new byte[32];
        TextBox[] textBoxes;
        Label[] labels;
        ArrayList aList;
        public SearchSensor()
        {
            InitializeComponent();
        }

        private void radioButton1_CheckedChanged(object sender, EventArgs e)
        {
            if (radioButton1.Checked)
            {
                groupBox1.Visible = true;
                groupBox2.Visible = false;
            }
            else
            {
                groupBox1.Visible = false;
                groupBox2.Visible = true;
            }
        }
        private byte[] loadInitialValues()
        {
            for (int i = 0; i <= 7; i++)
            {
                //Reading a single register at a time.
                Int16 status = HIDClass.ReadSingleRegister((byte)0, 1, (byte)i);
            }
        return GlobalVariables.WriteRegs;
        }

        private void Form1_Load(object sender, EventArgs e)
        {
            groupBox1.Visible = true;
            groupBox2.Visible = false;
            radioButton1.Checked = true;
            GlobalVariables.Default_Slave_Address = HIDDevInfo.I2C_Slave_Address = 0x88;           
            checkBox1.Checked = false;          
            SetRegister1();
            CopyReg = loadInitialValues();
        }
        private void SetRegister1()
        {
            Int16 status = HIDClass.WriteSingleRegister((byte)Convert.ToInt16(0xe3), 1, (byte)0);
        }

        private void checkBox1_CheckedChanged(object sender, EventArgs e)
        {
            if (checkBox1.Checked == true)
            {
                groupBox1.Enabled = true;
                groupBox2.Enabled = true;
            }
            else
            {
                groupBox1.Enabled = false;
                groupBox2.Enabled = false;
            }
        }


        private void readRegisters()
        {
            try
            {
                string proxData="",lsb="",msb="";
                txtRead.Text = "";
                labels = groupBox1.Controls.OfType<Label>().ToArray();
                    //Reading a single register at a time.
                    Int16 status = HIDClass.ReadSingleRegister((byte)0, 1, (byte)1);
                    if (status == 8)
                    {
                        for (int j = 0; j < 8; j++)                        
                             lsb += Convert.ToString(GlobalVariables.WriteRegs[2], 2).PadLeft(8, '0')[j].ToString();
                        for (int j = 0; j < 8; j++)
                             msb += Convert.ToString(GlobalVariables.WriteRegs[3], 2).PadLeft(8, '0')[j].ToString();
                           proxData =string.Concat(msb,lsb);
                          int value=binToDec(proxData);
                        
                        txtRead.Text =value.ToString();
                    if (txtRead.Text != "")
                    {
                    lblCount.Text = value.ToString();
                    for (int i = 1; i < textBoxes.Length; i++)
                    {
                       int check=Convert.ToInt32(aList[i-1]);
                        if (value>=check)
                        {
                            lblStatus.Text = labels[i-1].Text;
                            break;
                        }                  

                }

            }
                    }
                }         
            catch (Exception ex)
            {
                MessageBox.Show(ex.Message, "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }
        public int binToDec(string value)
        {
            double num;
            int decimal_val = 0, base_val = 1, rem;
            num = Convert.ToDouble(value);
            while (num > 0)
            {
                rem = Convert.ToInt32(num % 10);
                decimal_val = decimal_val + rem * base_val;
                num = num / 10;
                base_val = base_val * 2;
            }
            return decimal_val;
        }

        private void btnWrite_Click(object sender, EventArgs e)
        {
            int value = Convert.ToInt32(txtWrite.Text, 16);

            if (value >= 0 && value <= 255)
            {
                Int16 status = HIDClass.WriteSingleRegister((byte)Convert.ToInt32(value), 1, (byte)1);
                MessageBox.Show(status.ToString());
                txtRead.Text = "";
            }
           
        }

        private void btnRead_Click(object sender, EventArgs e)
        {
            readRegisters();
          
        }

        private void refreshTimer_Tick(object sender, EventArgs e)
        {
            readRegisters();
            
        }

        private void btnContinous_CheckedChanged(object sender, EventArgs e)
        {
            if (btnContinous.Checked == true)
                refreshTimer.Start();
            else
            {
                refreshTimer.Stop();
                refreshTimer.Dispose();
            }
        }

        private void btnSubmitPaper_Click(object sender, EventArgs e)
        {
             aList = new ArrayList();
             textBoxes = groupBox1.Controls.OfType<TextBox>().ToArray();
            for(int i=1;i<textBoxes.Length;i++)
            {
                aList.Add(textBoxes[i].Text);
            }
          
        }
    }
}
