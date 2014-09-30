using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Collections;

namespace layOut
{
    public class Algo
    {
        public static NewSensor NS = NewSensor.sensorObj;
        public static float calcSlope(int[] LUT)
        {
            float slope = 0.0f, sum = 0.0f;
            int i = 10, count = 0;
            try
            {
                //max index set according to tray selected
                count = NS.btnPaperTray.Checked == true ? count = 19 : count = 20;
                while (i < count)
                {
                    //if next value is 0 loop will break
                    if (LUT[i + 1] == 0)
                        break;
                    // else add the differences of consecutive values
                    else
                    {
                        sum = sum + (LUT[i + 1] - LUT[i]);
                        i++;
                    }
                    //calculate slope by calculating average of the vales, if paper tray is selected divide by 10 also b'coz there are sets of 10 papers 
                    slope = NS.btnPaperTray.Checked == true ? sum / ((i - 10) * 10) : sum / (i - 10);
                }
                return slope;
            }
            catch (Exception ex)
            {
                throw ex;
            }
        }

        public static float calcActualSlope(ArrayList slopeList)
        {
            float actualSlope = 0.0f;
            int diff = 0;
            try
            {
                //find the average of 3 stored values
                for (int i = 0; i < 2; i++)
                    diff = diff + (Convert.ToInt32(slopeList[i]) - Convert.ToInt32(slopeList[i + 1]));
                //divide by 10 in case of paper tray
                // actualSlope = NS.btnPaperTray.Checked == true ? diff / (2 * 10) : diff / 2;
                actualSlope = diff / 2;
                return actualSlope;
            }
            catch (Exception ex)
            {
                throw ex;
            }
        }
        public static int matchSlope(float actualSlope, float[] slopeList)
        {
            float average = 0.0f;
            try
            {
                //if the calculated slope is less than or equal to min value return 1 as index
                if (actualSlope <= (float)slopeList[0])
                    return 1;
                //if calculated slope is greater than or equal to max value return max index value 
                if (actualSlope >= slopeList[slopeList.Length - 1])
                    return slopeList.Length;
                for (int i = 0; i < slopeList.Length; i++)
                {
                    //if calculated slope value match exactly with any value then return that index
                    if ((float)slopeList[i] == actualSlope)
                        return i + 1;
                    //if slope value is less than any vaue in the list
                    else if (actualSlope < (float)slopeList[i + 1])
                    {
                        //find average of that index and its previous index value
                        average = ((float)slopeList[i] + (float)slopeList[i + 1]) / 2;
                        //if slope is less than average return smaller vaued index else greater valued index
                        if (actualSlope < average)
                            return i + 1;
                        else
                            return i + 2;
                    }
                }
                return 0;
            }
            catch (Exception ex)
            {
                throw ex;
            }
        }

        public static int calcGreaterPage(int colIndex, int proxData, int rowIndex)
        {
            int x = 0;
            float diffSlope = 0.0f, averageSlope = 0.0f, exactValue = 0.0f;
            try
            {
                //calculate average value of selected index and next index value
                diffSlope = (Convert.ToInt32(NS.gridPaperTray.Rows[rowIndex + 1].Cells[colIndex].Value) - Convert.ToInt32(NS.gridPaperTray.Rows[rowIndex].Cells[colIndex].Value)) / 10f;
                //compare data with the value at selected index + average difference no. of times, until the data value is greater than this value 
                while (proxData > Convert.ToInt32(NS.gridPaperTray.Rows[rowIndex].Cells[colIndex].Value) + (diffSlope * x))
                    x++;
                //store one less count to find lesser value
                x--;
                //store this value which is lesser than data value
                exactValue = Convert.ToInt32(NS.gridPaperTray.Rows[rowIndex].Cells[colIndex].Value) + (diffSlope * x);
                //divide the average by 2
                averageSlope = diffSlope / 2f;
                //now add calculated value with this average and compare with data value if data value is less than this value return index of lesser value else of greater value  
                if (proxData < exactValue + averageSlope)
                    return ((((rowIndex % 10) + 1) * 10) + x);
                else
                    return ((((rowIndex % 10) + 1) * 10) + x + 1);
            }
            catch (Exception ex)
            {
                throw ex;
            }
        }

        public static int calcLesserPage(int colIndex, int proxData, int rowIndex)
        {
            try
            {
                //find average of selected index value and its next value
                float average = (Convert.ToInt32(NS.gridPaperTray.Rows[rowIndex].Cells[colIndex].Value) + Convert.ToInt32(NS.gridPaperTray.Rows[rowIndex + 1].Cells[colIndex].Value)) / 2f;
                //if this average is less than data value return greater valued index else lesser valued index
                if (average < proxData)
                    return Convert.ToInt32(NS.gridPaperTray.Rows[rowIndex + 1].Cells[0].Value);
                else
                    return Convert.ToInt32(NS.gridPaperTray.Rows[rowIndex].Cells[0].Value);

            }
            catch (Exception ex)
            {
                throw ex;
            }
        }
    }

}