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
using System.Xml;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using System.Diagnostics;
using Inersil_WPFV2.Repository;
namespace layOut
{
    public partial class NewSensor : Form
    {
        #region Global Variables

        int[] ar;
        float[] paperSlope;
        float[] photoSlope;
        ArrayList alist,slopeList,photoSlopeList;
        float actualSlope;
        bool shwMsg = false;
        int index=0,photoIndex=0,paperCount=0,photoCount=0;
        byte[] CopyReg = new byte[32];
        public static NewSensor sensorObj { get; set; }
        #endregion

        #region Generic Form Method 
        public NewSensor()
        {
            InitializeComponent();
            sensorObj = this;
        }

        private void NewSensor_Load(object sender, EventArgs e)
        {
            //array to store readings of a single paper type
            ar = new int[21];
            //array list to store readings to take average
            alist = new ArrayList();
            //array list to store slopes of all paper types
            slopeList = new ArrayList();
            //array list to store slopes of photo paper types
            photoSlopeList = new ArrayList();
            gridPaperTray.Columns["paperNo"].Frozen = true;
            gridPaperTray.AutoGenerateColumns = true;
            gridPaperTray.DoubleBuffered(true);
            try
            {
                if (DeviceUtil.SearchDevice(StartUpForm.device_path))
                {
                    GlobalVariables.Default_Slave_Address = HIDDevInfo.I2C_Slave_Address = 0x88;
                    SetRegister1();
                    CopyReg = loadInitialValues();
                    //paper tray is selected by default
                    btnPaperTray.Checked = true;
                    //once mode is selected by default
                    btnOnce.Checked = true;
                    //stop button disabled in once mode
                    btnStop.Enabled = false;
                    //initialize registers
                    CopyReg = loadInitialValues();
                    readData();
                    //display selected paper in text box to edit values
                    txtPaperType.Invoke((MethodInvoker)(() => txtPaperType.Text = comboPaperType.SelectedItem.ToString()));
                    //make all settings according to paper tray
                    paperTraySelect();
                    //make capture button enabled
                    btnCapture.Enabled = true;
                    //make all papers values non-editable
                    foreach (DataGridViewColumn column in gridPaperTray.Columns)
                    {
                        column.SortMode = DataGridViewColumnSortMode.NotSortable;
                    }
                }
            }
            catch (Exception ex)
            {
                MessageBox.Show(ex.Message, "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }

        }

        #endregion

        #region Sensor Read Methods

        private void SetRegister1()
        {
            try
            {
                //set 0 and 1 registers with values 0xe3 and 0x54  
                Int16 status = HIDClass.WriteSingleRegister((byte)Convert.ToInt16(0xe3), 1, (byte)0);
                status = HIDClass.WriteSingleRegister((byte)Convert.ToInt16(0x54), 1, (byte)1);
            }
            catch (Exception ex)
            {
                MessageBox.Show(ex.Message, "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }      
        private byte[] loadInitialValues()
        {
            try
            {
                for (int i = 0; i <= 7; i++)
                {
                    //Reading a single register at a time.
                    Int16 status = HIDClass.ReadSingleRegister((byte)0, 1, (byte)i);
                }
               
            }
            catch (Exception ex)
            {
                MessageBox.Show(ex.Message, "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
                
            }
            return GlobalVariables.WriteRegs;
        }
        private int readRegisters()
        {
            int lsb, msb, value = 0;           
            try
            {
                if (shwMsg == true)
                {
                    MessageBox.Show("Please save LUT to get Correct Reading");
                    return value;
                }
                else
                {
                    //read status of 2nd register
                    Int16 status = HIDClass.ReadSingleRegister((byte)0, 1, (byte)2);
                    if (status == 8)
                    {
                        //read value in lsb
                        lsb = GlobalVariables.WriteRegs[2];
                        //read status of 3rd register
                        status = HIDClass.ReadSingleRegister((byte)0, 1, (byte)3);
                        if (status == 8)
                        {

                            //read value in msb
                            msb = GlobalVariables.WriteRegs[3];
                            //shift the value of 3rd register to make it MSB and make the 2 values as 32 bit value
                            value = (msb * 256) + lsb;
                            //store the value in a list
                            alist.Add(value);
                            //if continuous mode selected take average reading of 25 values
                            if (btnContinous.Checked == true)
                                averageReading();
                            //else display value in selected tray label
                            else
                            {
                                if (btnPaperTray.Checked == true)
                                {
                                    lblPaperCount.Invoke((MethodInvoker)(() => lblPaperCount.Text = value.ToString()));
                                    //if page index is determained calculate pages remaining
                                    if (index > 0)
                                        Task.Factory.StartNew(() => findPage(index));
                                }
                                else
                                {
                                    lblPhotoCount.Invoke((MethodInvoker)(() => lblPhotoCount.Text = value.ToString()));
                                    if (photoIndex > 0)
                                        Task.Factory.StartNew(() => findPage(photoIndex));
                                }
                            }
                        }
                    }
                    return value;
                }            
            }
            catch (Exception ex)
            {
                MessageBox.Show(ex.Message, "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return value;
            }
        }
        public void readData()
        {
            try
            {
            int[] data=new int[8];
            Int16 status = HIDClass.ReadSingleRegister((byte)0, 1, (byte)1);
            if (status == 8)
            {
                CopyReg[1]=GlobalVariables.WriteRegs[1];
                for (int i = 0; i <= 7; i++)
                {
                    double shift = Math.Pow(2, i);
                    data[i] = (byte)((CopyReg[1] & (byte)shift) >> i);
                }
                comboFreq.SelectedIndex=data[6]==0?0:1;
                string reso = data[3].ToString() + data[2].ToString();
                switch (reso)
                {
                    case "00": comboReso.SelectedIndex=0; break;
                    case "01": comboReso.SelectedIndex = 1; break;
                    case "10": comboReso.SelectedIndex = 2; break;
                    case "11": comboReso.SelectedIndex = 3; break;
                }
                comboScheme.SelectedIndex = data[7] == 0 ? 0 : 1;
                string range = data[1].ToString() + data[0].ToString();
                switch (range)
                {
                    case "00": comboRange.SelectedIndex=0; break;
                    case "01": comboRange.SelectedIndex = 1; break;
                    case "10": comboRange.SelectedIndex = 2; break;
                    case "11": comboRange.SelectedIndex = 3; break;
                }
                string IRDR = data[5].ToString() + data[4].ToString();
                switch (IRDR)
                {
                    case "00": comboIRDR.SelectedIndex=0; break;
                    case "01": comboIRDR.SelectedIndex = 1; break;
                    case "10": comboIRDR.SelectedIndex = 2; break;
                    case "11": comboIRDR.SelectedIndex = 3; break;
                }
            }
            }
            catch (Exception ex)
            {
                MessageBox.Show(ex.Message, "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }
        public void averageReading()
        {
            int sum = 0, average = 0;
            try
            {
                if (alist.Count >= 30)
                {
                    for (int i = 0; i < alist.Count; i++)
                        sum = sum + Convert.ToInt32(alist[i]);
                    average = sum / 30;
                    alist.Clear();
                    if (btnPaperTray.Checked == true)
                    {
                        lblPaperCount.Invoke((MethodInvoker)(() => lblPaperCount.Text = average.ToString()));
                        if (index > 0)
                            Task.Factory.StartNew(() => findPage(index));
                    }
                    else if (btnPhotoTray.Checked == true)
                    {
                        lblPhotoCount.Invoke((MethodInvoker)(() => lblPhotoCount.Text = average.ToString()));
                        if (photoIndex > 0)
                            Task.Factory.StartNew(() => findPage(photoIndex));
                    }                    
                }
            }
            catch (Exception ex)
            {
             MessageBox.Show(ex.Message, "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }
        private void refreshTimer_Tick(object sender, EventArgs e)
        {            
                readRegisters();          
        }

        #endregion

        #region Form Elements Click Events Methods
        private void btnPaperTray_CheckedChanged(object sender, EventArgs e)
        {
            try
            {
            if (btnPaperTray.Checked == true)
            {
                paperTraySelect();
                lblPhotoCaptureCount.Invoke((MethodInvoker)(() =>lblPhotoCaptureCount.Visible = false));
                lblCount.Invoke((MethodInvoker)(() => lblCount.Visible = true));
                lblType.Invoke((MethodInvoker)(() => lblType.Visible = true));
                lblPhotoType.Invoke((MethodInvoker)(() =>  lblPhotoType.Visible = false));
                lblPhotoCount.Invoke((MethodInvoker)(() => lblPhotoCount.Text = ""));
                lblPhotoStatus.Invoke((MethodInvoker)(() => lblPhotoStatus.Text = ""));
                for (int i = 1; i < gridPaperTray.Columns.Count; i++)
                {
                    gridPaperTray.Columns[i].DefaultCellStyle.BackColor = Color.White;
                    gridPaperTray.Columns[i].ReadOnly = true;
                }
            }
            else
            {
                photoTraySelect();             
                lblPhotoCaptureCount.Invoke((MethodInvoker)(() =>lblPhotoCaptureCount.Visible = true));
                lblCount.Invoke((MethodInvoker)(() => lblCount.Visible = false));
                lblType.Invoke((MethodInvoker)(() => lblType.Visible = false));
                lblPhotoType.Invoke((MethodInvoker)(() => lblPhotoType.Visible = true));
                lblPaperCount.Invoke((MethodInvoker)(() => lblPaperCount.Text = ""));
                lblPaperStatus.Invoke((MethodInvoker)(() => lblPaperStatus.Text = ""));
                for (int i = 1; i < gridPaperTray.Columns.Count; i++)
                {
                    gridPaperTray.Columns[i].DefaultCellStyle.BackColor = Color.White;
                    gridPaperTray.Columns[i].ReadOnly = true;
                }
            }
            }
            catch (Exception ex)
            {
                MessageBox.Show(ex.Message, "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }
        private void btnSubmit_Click(object sender, EventArgs e)
        {
            int nodeCount;
            bool add = true;
            try
            {              
                //all columns must have value else show error message
                for (int i = 0; i < gridPaperTray.Rows.Count; i++)
                {
                    for (int j = 1; j < gridPaperTray.Columns.Count; j++)
                    {
                        if (gridPaperTray.Rows[i].Cells[j].Value == null)
                        {
                            MessageBox.Show("Please fill values in all columns");
                            return;
                        }
                    }
                }
                //create xml document
                XmlDocument paperRecord = new XmlDocument();
                 string path = btnPaperTray.Checked == true ? Directory.GetCurrentDirectory() + "\\paperTray.xml" : Directory.GetCurrentDirectory() + "\\photoTray.xml";
                //load saved xml file in xml document
                paperRecord.Load(path);
                //select root node
                XmlNode RootNode = btnPaperTray.Checked == true ? paperRecord.SelectSingleNode("papertray") : paperRecord.SelectSingleNode("phototray");
                DataSet ds = new DataSet();
                ds.ReadXml(path);
                int col = 1;
                XmlNode typeNode=null;
                for (col = 1; col < gridPaperTray.Columns.Count; col++)
                {
                    for (nodeCount = 0; nodeCount < RootNode.ChildNodes.Count; nodeCount++)
                     {
                         if (gridPaperTray.Columns[col].HeaderText.ToUpper().Trim() == RootNode.ChildNodes[nodeCount].Attributes["name"].Value.ToUpper().Trim())
                         {
                             add = false;
                             typeNode = RootNode.ChildNodes[nodeCount];
                             break;
                         }
                         else
                             add = true;
                   }                      
                    if(add==true)
                    {
                        XmlNode node1, RecordNode;
                        XmlNode paperNode = paperRecord.CreateNode(XmlNodeType.Element, "type", "");
                        XmlAttribute attr = paperRecord.CreateAttribute("name");
                        foreach (XmlNode node in paperRecord.ChildNodes[1].ChildNodes)
                        {
                            node1 = node;
                            if (node1.Name == "type")
                            {
                                attr.Value = gridPaperTray.Columns[col].HeaderText;
                                paperNode.Attributes.Append(attr);
                                string value = txtPaperType.Text;
                                RecordNode = RootNode.AppendChild(paperNode);
                                for (int k = 1; k <= gridPaperTray.Rows.Count; k++)
                                {
                                    XmlNode childNode = paperNode.AppendChild(paperRecord.CreateNode(XmlNodeType.Element, "row", ""));
                                    childNode.AppendChild(paperRecord.CreateNode(XmlNodeType.Element, "id", "")).InnerText = k.ToString();                               
                                    childNode.AppendChild(paperRecord.CreateNode(XmlNodeType.Element, "value", "")).InnerText = gridPaperTray.Rows[k - 1].Cells[col].Value.ToString();                                  
                                }
                                paperRecord.Save(path);
                                break;
                            }
                            break;
                        }                   
                       
                    }    
                    else 
                    {
                        for (int j = 0; j <typeNode.ChildNodes.Count; j++)
                           typeNode.ChildNodes[j].ChildNodes[1].InnerText = gridPaperTray.Rows[j].Cells[col].Value.ToString();
                        paperRecord.Save(path);
                    }
                  
                }
                MessageBox.Show("Record saved Successfully");
                shwMsg = false;
                grpStartRead.Invoke((MethodInvoker)(() => grpStartRead.Enabled = false));
                createSlopeList();
                grpStartRead.Invoke((MethodInvoker)(() => grpStartRead.Enabled = true));
            }
            catch (Exception ex)
            {
                MessageBox.Show(ex.Message, "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
            finally
            {
                for (int i = 1; i < gridPaperTray.Columns.Count; i++)
                {
                    gridPaperTray.Columns[i].DefaultCellStyle.BackColor = Color.White;
                    gridPaperTray.Columns[i].ReadOnly = true;
                    for (int j = 0; j < gridPaperTray.Rows.Count; j++)
                        gridPaperTray.Rows[j].Cells[i].Style.ApplyStyle(gridPaperTray.Columns[i].DefaultCellStyle);
                }
            }
        }        
        private void btnStart_Click(object sender, EventArgs e)
        {
            try
            {
                if (DeviceUtil.SearchDevice(StartUpForm.device_path))
                {
                    GlobalVariables.Default_Slave_Address = HIDDevInfo.I2C_Slave_Address = 0x88;
                    SetRegister1();                    
                    CopyReg = loadInitialValues();
                    if (btnContinous.Checked && refreshTimer.Enabled==false)
                    {
                        refreshTimer.Start();
                        btnStart.Invoke((MethodInvoker)(() => btnStart.Enabled = false));
                        if (slopeList.Count != 3)
                            lblPaperStatus.Text = "";

                    }
                    else
                    {
                        refreshTimer.Stop();
                        refreshTimer.Dispose();
                        btnStart.Invoke((MethodInvoker)(() => btnStart.Enabled = true));
                        if (photoSlopeList.Count != 3)
                            lblPhotoStatus.Text = "";
                        readRegisters();
                    }
                }
                else
                {
                    refreshTimer.Stop();
                    refreshTimer.Dispose();
                    
                }
            }
            catch (Exception ex)
            {
                MessageBox.Show(ex.Message, "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }
        private void comboBox1_SelectedIndexChanged(object sender, EventArgs e)
        {
            txtPaperType.Text = comboPaperType.SelectedItem.ToString();            
        }
        private void btnContinous_CheckedChanged(object sender, EventArgs e)
        {
            modeSelect();
        }
        private void btnAdd_Click(object sender, EventArgs e)
        {
            int index=0;
            try
            {
            for (int i = 0; i < gridPaperTray.Columns.Count; i++)
            {
                if (gridPaperTray.Columns[i].HeaderText.ToUpper().Trim().Equals(txtPaperType.Text.ToUpper().Trim()))
                {
                    index = i;
                    break;
                }
            }
                if(index>0)
                {                  
                     DataGridViewColumn dataGridViewColumn = gridPaperTray.Columns[index];
                     dataGridViewColumn.HeaderCell.Style.BackColor = Color.Aquamarine;
                     gridPaperTray.Columns[index].ReadOnly = false;
                     gridPaperTray.Columns[index].DefaultCellStyle.BackColor = Color.Aquamarine;
                 //   gridPaperTray.ColumnHeadersDefaultCellStyle.BackColor = Color.GreenYellow;
                     for (int j = 0; j < gridPaperTray.Rows.Count; j++)
                         gridPaperTray.Rows[j].Cells[index].Style.ApplyStyle(gridPaperTray.Columns[index].DefaultCellStyle);
                    
                    //dataGridViewColumn.EnableHeadersVisualStyles = false;
                    gridPaperTray.CurrentCell = gridPaperTray.Rows[0].Cells[index];
                }
                else
                    MessageBox.Show("Paper type is not available to edit!!!!!!");                                    
            }
            
            catch (Exception ex)
            {
                MessageBox.Show(ex.Message, "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }
        private void btnStop_Click(object sender, EventArgs e)
        {
            refreshTimer.Stop();
            refreshTimer.Dispose();
            btnStart.Invoke((MethodInvoker)(() => btnStart.Enabled = true));            
            lblPaperCount.Text = "";
            lblPaperStatus.Text = "";
            lblPhotoCount.Text = "";
            lblPhotoStatus.Text = "";
        }
        private void btnCapture_Click(object sender, EventArgs e)
        {
            int value, listCount = 0;         
            try
            {
                
                if (btnPaperTray.Checked == true && lblPaperCount.Text != "")
                {
                    value = Convert.ToInt32(lblPaperCount.Text);
                    //add prox data to a list
                    addData(slopeList,value);
                    paperCount++;
                    lblCount.Text = paperCount.ToString();
                }
                else if (btnPhotoTray.Checked == true && lblPhotoCount.Text != "")
                {
                    value = Convert.ToInt32(lblPhotoCount.Text);
                    //add prox data to a list
                    addData(photoSlopeList, value);
                    photoCount++;
                    lblPhotoCaptureCount.Text = photoCount.ToString();
                }
                //if list count is 3 then calculate actual slope of particular paper
                listCount = btnPaperTray.Checked == true ? slopeList.Count : photoSlopeList.Count;
              //  btnCapture.Invoke((MethodInvoker)(() => btnCapture.Enabled = listCount == 3 ? false : true)); 
                if (listCount >= 3)
                {
                    actualSlope = btnPaperTray.Checked == true ? Algo.calcActualSlope(slopeList) : Algo.calcActualSlope(photoSlopeList);
                    if (btnPaperTray.Checked == true)
                    {
                       index= Algo.matchSlope(actualSlope, paperSlope);
                       if (index > 0)
                            Task.Factory.StartNew(() => findPage(index));
                    }
                    else
                    {
                        photoIndex = Algo.matchSlope(actualSlope, photoSlope);
                        if (photoIndex > 0)
                            Task.Factory.StartNew(() => findPage(photoIndex));
                    }                   
                    if (btnPaperTray.Checked == true)
                    {
                        //slopeList.Clear();
                        lblType.Text = gridPaperTray.Columns[index].HeaderText;
                    }
                    else
                    {
                        //photoSlopeList.Clear();
                        lblPhotoType.Text = gridPaperTray.Columns[photoIndex].HeaderText;
                    }
                // btnCapture.Invoke((MethodInvoker)(() => btnCapture.Enabled =false));
                }
            }
            catch (Exception ex)
            {
                MessageBox.Show(ex.Message, "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);               
            }           
        }
        private void btnOnce_CheckedChanged(object sender, EventArgs e)
        {
            try
            {
                if (btnOnce.Checked == true)
                {
                    refreshTimer.Stop();
                    refreshTimer.Dispose();
                }
            }
            catch (Exception ex)
            {
                MessageBox.Show(ex.Message, "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }
        private void btnRefill_Click(object sender, EventArgs e)
        {
            refresh();
        }
        private void NewSensor_FormClosed(object sender, FormClosedEventArgs e)
        {
            try
            {
            if (refreshTimer.Enabled == true)
            {
                refreshTimer.Stop();
                refreshTimer.Dispose();
            }
            Process.GetCurrentProcess().Kill();
            }
            catch (Exception ex)
            {
                MessageBox.Show(ex.Message, "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }
        private void btnAddNew_Click(object sender, EventArgs e)
        {
            bool add = true;
            try
            {
                if (txtPaperType.Text != "")
                {
                    for (int i = 0; i < comboPaperType.Items.Count; i++)
                    {
                        if (comboPaperType.Items[i].ToString().ToUpper().Trim() == txtPaperType.Text.ToUpper().Trim())
                        {
                            add = false;
                            MessageBox.Show("Page already exists");
                            break;
                        }
                        else
                            add = true;
                    }
                    if (add == true)
                    {
                        DataGridViewColumn dcolumn = new DataGridViewColumn();
                        dcolumn.SortMode = DataGridViewColumnSortMode.NotSortable;
                        dcolumn.Name = "Column" + gridPaperTray.Columns.Count;
                        dcolumn.ReadOnly = true;
                        dcolumn.DataPropertyName = "Column" + gridPaperTray.Columns.Count;
                        dcolumn.DefaultCellStyle = gridPaperTray.DefaultCellStyle;
                        dcolumn.CellTemplate = new DataGridViewTextBoxCell();
                        dcolumn.HeaderText = txtPaperType.Text;
                        gridPaperTray.Columns.Add(dcolumn);
                        comboPaperType.Items.Add(txtPaperType.Text);
                        gridPaperTray.CurrentCell = gridPaperTray.Rows[0].Cells[gridPaperTray.Columns.Count - 1];
                    }
                }
            }
            catch (Exception ex)
            {
                MessageBox.Show(ex.Message, "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }               
        private void btnRemove_Click(object sender, EventArgs e)
        {
            try
            {
                if (gridPaperTray.Columns.Count > 2)
                {
                    XmlNode node1;
                    //create an xml document
                    XmlDocument xm = new XmlDocument();
                    //load xml file related to tray selected
                    string path = btnPaperTray.Checked == true ? Directory.GetCurrentDirectory() + "\\paperTray.xml" : Directory.GetCurrentDirectory() + "\\photoTray.xml";
                    xm.Load(path);
                    //select root node
                    XmlNode RootNode = btnPaperTray.Checked == true ? xm.SelectSingleNode("papertray") : xm.SelectSingleNode("phototray");
                    for (int nodeCount = 0; nodeCount < RootNode.ChildNodes.Count; nodeCount++)
                    {
                        if (RootNode.ChildNodes[nodeCount].Attributes["name"].Value == txtPaperType.Text)
                        {
                            node1 = RootNode.ChildNodes[nodeCount];
                            node1.ParentNode.RemoveChild(node1);
                        }
                    }
                    xm.Save(path);
                    MessageBox.Show("Paper Removed Successfully");
                    readXMLPaper();
                }
                else
                    MessageBox.Show("You can't remove all the paper types");
            }
            catch (Exception ex)
            {
                MessageBox.Show(ex.Message, "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }
        private void btnSave_Click(object sender, EventArgs e)
        {
            try
            {
                int[] data = new int[8];
                bool result = true;
                string value = "";
                if (DeviceUtil.SearchDevice(StartUpForm.device_path))
                {
                    GlobalVariables.Default_Slave_Address = HIDDevInfo.I2C_Slave_Address = 0x88;
                    SetRegister1();
                    CopyReg = loadInitialValues();

                    string range = comboRange.SelectedItem.ToString();
                    string reso = comboReso.SelectedItem.ToString();
                    string IRDR = comboIRDR.SelectedItem.ToString();
                    string freq = comboFreq.SelectedItem.ToString();
                    string scheme = comboScheme.SelectedItem.ToString();
                    switch (reso)
                    {
                        case "16": data[3] = 0; data[2] = 0; break;
                        case "12": data[3] = 0; data[2] = 1; break;
                        case "8": data[3] = 1; data[2] = 0; break;
                        case "4": data[3] = 1; data[2] = 1; break;
                    }
                    //lblScheme.Text = data[7] == 0 ? "IR from LED" : "IR from LED with IR Rejection";
                    //string range = data[1].ToString() + data[0].ToString();
                    switch (range)
                    {
                        case "1000": data[1] = 0; data[0] = 0; break;
                        case "4000": data[1] = 0; data[0] = 1; break;
                        case "16000": data[1] = 1; data[0] = 0; break;
                        case "64000": data[1] = 1; data[0] = 1; break;
                    }
                    // string IRDR = data[5].ToString() + data[4].ToString();
                    switch (IRDR)
                    {
                        case "12.5mA": data[5] = 0; data[4] = 0; break;
                        case "25mA": data[5] = 0; data[4] = 1; break;
                        case "50mA": data[5] = 1; data[4] = 0; break;
                        case "100mA": data[5] = 1; data[4] = 1; break;
                    }
                    data[6] = freq == "DC" ? 0 : 1;
                    data[7] = scheme == "IR from LED" ? 0 : 1;
                    for (int i = 7; i >= 0; i--)
                        value += data[i].ToString();
                    int hexvalue = Convert.ToInt32(value, 2);

                    Int16 status = HIDClass.WriteSingleRegister((byte)hexvalue, 1, (byte)1);
                    if (status != 8)
                        result = false;

                    if (result == true)
                        MessageBox.Show("Values configured Successfully");
                    else
                        MessageBox.Show("Error!!!!Values not configured");
                }
                readData();
            }
            catch
            {
            }
        }
        private void chkEnable_CheckedChanged(object sender, EventArgs e)
        {
            if (chkEnable.Checked == true)
                grpReg.Enabled = true;
            else
                grpReg.Enabled = false;
        }

        #endregion

        #region Paper Tray Methods
        public void paperGrid()
        {
            try
            {
                ArrayList rowList = new ArrayList();
                for (int i = 0; i < 10; i++)
                    rowList.Add(i);
                for (int j = 10; j <= 100; j += 10)
                    rowList.Add(j);               
                for (int i = 0; i < rowList.Count; i++)
                   gridPaperTray.Invoke((MethodInvoker)(() =>  gridPaperTray.Rows.Add(rowList[i].ToString())));              
            }
            catch (Exception ex)
            {
                MessageBox.Show(ex.Message, "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }      
        public void paperTraySelect()
        {
            try
            {
                gridPaperTray.Invoke((MethodInvoker)(() => grpPaperTray.Enabled = true));
                grpPhotoTray.Invoke((MethodInvoker)(() => grpPhotoTray.Enabled = false));
                lblPaperType.Invoke((MethodInvoker)(() =>lblPaperType.Text = "Paper Tray"));
                readXMLPaper();             
                for (int j = 1; j < gridPaperTray.Columns.Count; j++)
                {
                    for (int i = 0; i < gridPaperTray.Rows.Count; i++)
                    {
                        if (gridPaperTray.Rows[i].Cells[j].Value.ToString() == "")
                        {
                            MessageBox.Show("Please fill values in all columns");
                            return;
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                MessageBox.Show(ex.Message, "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }           
        }     
        #endregion

        #region Photo Tray Methods
        public void photoTraySelect()
        {
            try
            {
                //change the grid view columns according to tray selected               
               readXMLPaper();            
               grpPaperTray.Invoke((MethodInvoker)(() =>  grpPaperTray.Enabled = false));
               grpPhotoTray.Invoke((MethodInvoker)(() => grpPhotoTray.Enabled = true));
               lblPaperType.Invoke((MethodInvoker)(() => lblPaperType.Text = "Photo Tray"));  
            }
            catch (Exception ex)
            {
                MessageBox.Show(ex.Message, "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }
        public void photoGrid()
        {
            try
            {
                ArrayList rowList = new ArrayList();
                //add rows in list
                for (int i = 0; i <= 20; i++)
                    rowList.Add(i);              
                //add rows according to list
                for (int i = 0; i < rowList.Count; i++)
                    gridPaperTray.Invoke((MethodInvoker)(() =>  gridPaperTray.Rows.Add(rowList[i].ToString())));               
            }
            catch (Exception ex)
            {
                MessageBox.Show(ex.Message, "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }
        public void modeSelect()
        {
            if (btnContinous.Checked)
            {

                btnStart.Invoke((MethodInvoker)(() => btnStart.Enabled = true));
                btnStop.Invoke((MethodInvoker)(() => btnStop.Enabled = true));
            }
            else
            {
                btnStart.Invoke((MethodInvoker)(() => btnStart.Enabled = true));
                btnStop.Invoke((MethodInvoker)(() => btnStop.Enabled = false));
            }
        }           
        #endregion  

        #region Methods Related to XML
        public void readXMLPaper()
        {  
            gridPaperTray.Rows.Clear();
            gridPaperTray.Columns.Clear();
            gridPaperTray.AutoGenerateColumns = true;
            comboPaperType.Items.Clear();
            try
            {
                int count = 0;
                //create an xml document
                XmlDocument xm = new XmlDocument();
                //load xml file related to tray selected
                if (btnPaperTray.Checked == true)
                {
                    xm.Load(Directory.GetCurrentDirectory() + "\\paperTray.xml");
                    count = 20;
                }
                else
                {
                    xm.Load(Directory.GetCurrentDirectory() + "\\photoTray.xml");
                    count = 21;
                }
                XmlNode node1;
                XmlNode valueNode;
                string dataValue = "";
                DataGridViewColumn NoCol = new DataGridViewColumn();
                //col.ColumnName = "column" + columnCount;
                DataGridViewCell NoCell = new DataGridViewTextBoxCell();
                NoCol.DataPropertyName = "column" + gridPaperTray.Columns.Count;
                NoCell.Style = gridPaperTray.DefaultCellStyle;
                NoCol.CellTemplate = NoCell;
                NoCol.Name = "paperNo";
                NoCol.HeaderText = "Paper No.";
                NoCol.ReadOnly = true;
                NoCol.Width = 70;
                gridPaperTray.Columns.Add(NoCol);               
                gridPaperTray.Columns["paperNo"].Frozen = true;
                if (btnPaperTray.Checked == true)
                    paperGrid();
                else
                    photoGrid();
                //traverse the xml file 
                foreach (XmlNode node in xm.ChildNodes[1].ChildNodes)
                {
                    node1 = node;
                    DataGridViewColumn col = new DataGridViewColumn();
                    DataGridViewCell newCell = new DataGridViewTextBoxCell();                    
                    newCell.Style = gridPaperTray.DefaultCellStyle;
                    col.CellTemplate = newCell;
                    col.SortMode = DataGridViewColumnSortMode.NotSortable;
                    //take the value of paper type from xml file
                    string value = node1.Attributes["name"].Value.ToString();                    
                    gridPaperTray.Columns.Add(col);
                    comboPaperType.Items.Add(value);
                    //find the details of device
                    if (node1.Name == "type")
                    {
                        col.HeaderText = value;                        
                        int i = 0;
                        {
                            for (int val = 0; val < node1.ChildNodes.Count; val++)
                            //foreach (XmlNode item in node1.ChildNodes)
                            {
                                if (i < count)
                                {
                                    valueNode = node1.ChildNodes[val];
                                    // valueNode = childNode;
                                    for (int j = 0; j < valueNode.ChildNodes.Count; j++)
                                    {
                                        //find the node with value
                                        if (valueNode.ChildNodes[j].Name == "value")
                                        {
                                            //take the value and assign it to grid view in respected column
                                            dataValue = valueNode.ChildNodes[j].InnerText;
                                            gridPaperTray.Invoke((MethodInvoker)(() => gridPaperTray.Rows[i].Cells[col.Index].Value = dataValue));
                                        }
                                    }
                                    i++;
                                }
                                else
                                    break;
                            }
                        }
                    }
                    gridPaperTray.Columns[0].DefaultCellStyle.BackColor = Color.Wheat;
                    DataGridViewColumn dataGridViewColumn = gridPaperTray.Columns[0];
                    dataGridViewColumn.HeaderCell.Style.BackColor = Color.Wheat;
                    for (int j = 0; j < gridPaperTray.Rows.Count; j++)
                        gridPaperTray.Rows[j].Cells[0].Style.ApplyStyle(gridPaperTray.Columns[0].DefaultCellStyle);
                }
                //create a slope list
                createSlopeList();
            }
            catch (Exception ex)
            {
                MessageBox.Show(ex.Message, "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
            finally
            {
                for (int i = 1; i < gridPaperTray.Columns.Count; i++)
                {
                    gridPaperTray.Columns[i].DefaultCellStyle.BackColor = Color.White;
                    gridPaperTray.Columns[i].ReadOnly = true;
                }
               // gridPaperTray.AllowUserToOrderColumns = true;
                comboPaperType.SelectedIndex = 0;
            }
        }
        #endregion       

        #region Calculation Methods
        public void addData(ArrayList list,int data)
        {
            try
            {
            if (list.Count >= 3)
            {
                for (int i = 1; i < 3; i++)
                    list[i - 1] = list[i];
                list[2]=data;
            }
            else
                list.Add(data);
            }
            catch (Exception ex)
            {
                MessageBox.Show(ex.Message, "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }
        public void createSlopeList()
        {
            try
            {
                float slope = 0.0f;
                //if paper tray is selected array will be of 20 size else of 21
                ar = btnPaperTray.Checked == true ? new int[20] : new int[21];
                //take list for storing slope of papers
                paperSlope = new float[gridPaperTray.ColumnCount-1];
                photoSlope = new float[gridPaperTray.ColumnCount-1];
                if (gridPaperTray.Rows.Count > 0)
                {
                    for (int sCount = 1; sCount <= gridPaperTray.ColumnCount - 1; sCount++)
                    {
                        //take values of each paper in an array 
                        for (int j = 0; j < gridPaperTray.Rows.Count; j++)
                        {
                            ar[j] = Convert.ToInt32(gridPaperTray.Rows[j].Cells[sCount].Value);
                        }
                        //send this array to calculate slope
                        slope = Algo.calcSlope(ar);
                        //store slope in the list
                        if (btnPaperTray.Checked == true)
                            paperSlope[sCount - 1] = slope;
                        else
                            photoSlope[sCount - 1] = slope;
                    }
                }
            }
            catch (Exception ex)
            {
                MessageBox.Show(ex.Message, "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }            
        public void findPage(int index)
        {
            int count,status=0;
            int rowIndex = gridPaperTray.Rows.Count - 1;
            try
            {
                //take the proxdata in a variable according to tray selected
                count = btnPaperTray.Checked == true ? Convert.ToInt32(lblPaperCount.Text) : Convert.ToInt32(lblPhotoCount.Text);
                //find the max index of row with non zero value
                for (int i = gridPaperTray.Rows.Count - 1; i >= 0; i--)
                {
                    if (Convert.ToInt32(gridPaperTray.Rows[i].Cells[index].Value) != 0)
                    {
                        rowIndex = i;
                        break;
                    }
                }
                //if data value is less than min value status is 0(page value)
                if (count <= Convert.ToInt32(gridPaperTray.Rows[0].Cells[index].Value))
                    status = 0;
                    //if data value is greater than max value status is max row index with non-zero value
                else if (count >= Convert.ToInt32(gridPaperTray.Rows[rowIndex].Cells[index].Value))
                    status = Convert.ToInt32(gridPaperTray.Rows[rowIndex].Cells[0].Value);
                else
                {
                    for (int j = rowIndex; j >= 0; j--)
                    {
                        //if value matches exactly with some value in the list assign its index to status
                        if (count == Convert.ToInt32(gridPaperTray.Rows[j].Cells[index].Value))
                            status = Convert.ToInt32(gridPaperTray.Rows[j].Cells[index].Value);
                            //if data vaue is greater than value of any column
                        else if (count > Convert.ToInt32(gridPaperTray.Rows[j].Cells[index].Value))
                        {
                            //if paper tray selcted then check if its index is greater than 10 then calculate status by calcgreaterpage method
                            if (btnPaperTray.Checked == true)
                            {
                                if (j < 10)
                                    status = Algo.calcLesserPage(index, count, j);
                                else
                                    status = Algo.calcGreaterPage(index, count, j);
                            }
                                //if photo tray selected calculate pages by lesserpage method
                            else
                                status = Algo.calcLesserPage(index, count, j);
                            break;
                        }
                    }
                }
                //diisplay value on selected tray label
                if (btnPaperTray.Checked == true)
                {
                    lblPaperStatus.Invoke((MethodInvoker)(() => lblPaperStatus.Text = status.ToString()));
                    lblPaperStatus.Invoke((MethodInvoker)(() => lblPaperStatus.Refresh()));
                }
                else
                {
                   lblPhotoStatus.Invoke((MethodInvoker)(() =>  lblPhotoStatus.Text = status.ToString()));
                   lblPhotoStatus.Invoke((MethodInvoker)(() =>  lblPhotoStatus.Refresh()));
                }
            }
            catch (Exception ex)
            {
                MessageBox.Show(ex.Message, "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }
        public void refresh()
        {
            //make refresh the page by reseting all the values          
            try
            {
                GlobalVariables.Default_Slave_Address = HIDDevInfo.I2C_Slave_Address = 0x88;
                 SetRegister1();
                btnOnce.Invoke((MethodInvoker)(() => btnOnce.Checked = true));
                CopyReg = loadInitialValues();
                txtPaperType.Invoke((MethodInvoker)(() => txtPaperType.Text = comboPaperType.SelectedItem.ToString()));
                if (btnPaperTray.Checked == true)
                {
                    paperTraySelect();
                    //paperTypeSelect();
                    lblCount.Text = "";
                    lblPaperCount.Text = "";
                    lblPaperStatus.Text = "";
                    lblType.Text = "";
                    index = 0;
                    paperCount = 0;
                    slopeList.Clear();
                }
                else
                {
                    photoTraySelect();
                    lblPhotoCaptureCount.Text = "";
                    lblPhotoType.Text = "";
                    lblPhotoCount.Text = "";
                    lblPhotoStatus.Text = "";
                    photoIndex = 0;
                    photoCount = 0;
                    photoSlopeList.Clear();
                }                
                btnCapture.Invoke((MethodInvoker)(() => btnCapture.Enabled = true));
            }
            catch (Exception ex)
            {
                MessageBox.Show(ex.Message, "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }
        #endregion     
       
    }
}
