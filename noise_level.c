/**
 * @file noise_level.c
 * @brief Functions to manage the noise gauge and draw the screen view
 * on the TFT
 *
 * @date June, 5th, 2020
 * @uthor: Enrico Miglino
 */

#include "noise_level.h"

/**
*
* GetAngle returns the value value to indicate on the gauge
* according to the value to show. The value for the max angle
* limit in the gauge design is defined in ABSOLUTE_MAX_NOISE
*
*/
static float GetAngle(int tDiff) {
  if (tDiff < 15000) {
    return  225 - 0.006 * tDiff ;
  }
  tDiff -= 15000;
  if (tDiff < 7500) {
    return  225 - 90 + 0.012 * tDiff ;
  }
  return 225;
}

/**
* DrawNeedle redraw the indicator at the desired position, corresponding to
* the scaled measured value
*
* @param p pointer to the PARAM structure
*/
static void DrawNeedle(void * p) {
	  PARAM * pParam;

	  pParam = (PARAM *)p;

	// Fixed background
	//
	if (pParam->AutoDevInfo.DrawFixed) {
		GUI_ClearRect (60, 80 + bmScaleR140.YSize, 60 + bmScaleR140.XSize - 1, 180);
		GUI_DrawBitmap(&bmScaleR140, 60, 80);
	}
	//
	// Moving needle
	//
	GUI_SetColor(GUI_WHITE);
	GUI_AA_FillPolygon(pParam->aPoints, countof(_aNeedle), MAG * 160, MAG * 220);
	//
	// Fixed foreground
	//
	if (pParam->AutoDevInfo.DrawFixed) {
		GUI_SetTextMode(GUI_TM_TRANS);
		GUI_SetColor(GUI_YELLOW);
		GUI_SetFont(&GUI_Font24B_ASCII);
	}
}

/**
 * Initialize the GUI, set the resolution and screen title
 */
void NoiseLevelInitGUI(void) {
	GUI_SetBkColor(GUI_BLACK);
	GUI_Clear();
	GUI_SetColor(GUI_WHITE);
	GUI_SetFont(&GUI_Font24_ASCII);
	GUI_DispStringHCenterAt("Noise Level", 160, 5);
	GUI_SetFont(&GUI_Font8x16);
	GUI_DispStringHCenterAt("Instant Microphone Detection", 160, 50);

	// Enable high resolution for antialiasing
	GUI_AA_EnableHiRes();
	GUI_AA_SetFactor(MAG);
}

//! Show the scale value.
void DrawScale(int noise) {
	PARAM Param;

	// Create GUI_AUTODEV-object
	GUI_MEMDEV_CreateAuto(&AutoDev);

	GUI_DrawBitmap(&bmScaleR140, 60, 80);

	// Show needle for the corresponding value
	// Get value to display an calculate polygon for needle
	Param.Angle = GetAngle(noise)* DEG2RAD;

	GUI_RotatePolygon(Param.aPoints, _aNeedle, countof(_aNeedle), Param.Angle);
	GUI_MEMDEV_DrawAuto(&AutoDev, &Param.AutoDevInfo, &DrawNeedle, &Param);
}

