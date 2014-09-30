namespace layOut
{
    partial class StartUpForm
    {
        /// <summary>
        /// Required designer variable.
        /// </summary>
        private System.ComponentModel.IContainer components = null;

        /// <summary>
        /// Clean up any resources being used.
        /// </summary>
        /// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
        protected override void Dispose(bool disposing)
        {
            if (disposing && (components != null))
            {
                components.Dispose();
            }
            base.Dispose(disposing);
        }

        #region Windows Form Designer generated code

        /// <summary>
        /// Required method for Designer support - do not modify
        /// the contents of this method with the code editor.
        /// </summary>
        private void InitializeComponent()
        {
            System.ComponentModel.ComponentResourceManager resources = new System.ComponentModel.ComponentResourceManager(typeof(StartUpForm));
            this.btnCancel = new System.Windows.Forms.Button();
            this.pictureBox1 = new System.Windows.Forms.PictureBox();
            this.btnConnect = new System.Windows.Forms.Button();
            this.comboDevice = new System.Windows.Forms.ComboBox();
            this.grpSelect = new System.Windows.Forms.GroupBox();
            ((System.ComponentModel.ISupportInitialize)(this.pictureBox1)).BeginInit();
            this.grpSelect.SuspendLayout();
            this.SuspendLayout();
            // 
            // btnCancel
            // 
            this.btnCancel.BackColor = System.Drawing.Color.Wheat;
            this.btnCancel.Location = new System.Drawing.Point(145, 92);
            this.btnCancel.Name = "btnCancel";
            this.btnCancel.Size = new System.Drawing.Size(98, 30);
            this.btnCancel.TabIndex = 3;
            this.btnCancel.Text = "Cancel";
            this.btnCancel.UseVisualStyleBackColor = false;
            this.btnCancel.Click += new System.EventHandler(this.btnCancel_Click);
            // 
            // pictureBox1
            // 
            this.pictureBox1.BackColor = System.Drawing.Color.Transparent;
            this.pictureBox1.BackgroundImageLayout = System.Windows.Forms.ImageLayout.Center;
            this.pictureBox1.Image = ((System.Drawing.Image)(resources.GetObject("pictureBox1.Image")));
            this.pictureBox1.InitialImage = null;
            this.pictureBox1.Location = new System.Drawing.Point(62, 27);
            this.pictureBox1.Name = "pictureBox1";
            this.pictureBox1.Size = new System.Drawing.Size(155, 40);
            this.pictureBox1.TabIndex = 23;
            this.pictureBox1.TabStop = false;
            // 
            // btnConnect
            // 
            this.btnConnect.BackColor = System.Drawing.Color.Wheat;
            this.btnConnect.Location = new System.Drawing.Point(18, 92);
            this.btnConnect.Name = "btnConnect";
            this.btnConnect.Size = new System.Drawing.Size(94, 30);
            this.btnConnect.TabIndex = 2;
            this.btnConnect.Text = "Connect";
            this.btnConnect.UseVisualStyleBackColor = false;
            this.btnConnect.Click += new System.EventHandler(this.btnConnect_Click);
            // 
            // comboDevice
            // 
            this.comboDevice.DropDownStyle = System.Windows.Forms.ComboBoxStyle.DropDownList;
            this.comboDevice.FormattingEnabled = true;
            this.comboDevice.Items.AddRange(new object[] {
            "ISL 29011"});
            this.comboDevice.Location = new System.Drawing.Point(18, 43);
            this.comboDevice.Name = "comboDevice";
            this.comboDevice.Size = new System.Drawing.Size(225, 24);
            this.comboDevice.TabIndex = 0;
            // 
            // grpSelect
            // 
            this.grpSelect.Controls.Add(this.btnCancel);
            this.grpSelect.Controls.Add(this.btnConnect);
            this.grpSelect.Controls.Add(this.comboDevice);
            this.grpSelect.Font = new System.Drawing.Font("Microsoft Sans Serif", 10F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.grpSelect.Location = new System.Drawing.Point(12, 87);
            this.grpSelect.Name = "grpSelect";
            this.grpSelect.Size = new System.Drawing.Size(260, 148);
            this.grpSelect.TabIndex = 24;
            this.grpSelect.TabStop = false;
            // 
            // StartUpForm
            // 
            this.AutoScaleDimensions = new System.Drawing.SizeF(6F, 13F);
            this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            this.BackColor = System.Drawing.Color.FromArgb(((int)(((byte)(187)))), ((int)(((byte)(187)))), ((int)(((byte)(187)))));
            this.ClientSize = new System.Drawing.Size(284, 262);
            this.Controls.Add(this.pictureBox1);
            this.Controls.Add(this.grpSelect);
            this.Icon = ((System.Drawing.Icon)(resources.GetObject("$this.Icon")));
            this.MaximizeBox = false;
            this.Name = "StartUpForm";
            this.Text = "Connect Device";
            this.Load += new System.EventHandler(this.StartUpForm_Load);
            ((System.ComponentModel.ISupportInitialize)(this.pictureBox1)).EndInit();
            this.grpSelect.ResumeLayout(false);
            this.ResumeLayout(false);

        }

        #endregion

        private System.Windows.Forms.Button btnCancel;
        private System.Windows.Forms.PictureBox pictureBox1;
        private System.Windows.Forms.Button btnConnect;
        private System.Windows.Forms.ComboBox comboDevice;
        private System.Windows.Forms.GroupBox grpSelect;
    }
}