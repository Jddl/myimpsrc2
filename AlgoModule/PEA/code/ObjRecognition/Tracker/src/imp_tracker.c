#include "imp_tracker.h"
#include "time.h"
#include "stdlib.h"
#include "highgui.h"
#include "cv.h"
#include "cxcore.h"
#include "stdio.h"
#include "math.h"

#define IMP_MIN_TGT_AREA          10
#define IMP_STABLE_THRESHOLD      20


typedef struct impTgtPosition_S
{
	IMP_U8 s32Used;
//	IMP_U32 u32Time;
//	IMP_U32 u32Type;
//	IMP_U32 u32TgtEvent;
	IMP_POINT_S stPt;
	IMP_RECT_S stRg;
	IMP_U32 u32AreaPixel;
//	IMP_U32 u32AreaActual;
	IMP_S32 s32Velocity;
	IMP_S32 s32Direction;
//	IMP_S32 s32DiffEnergy;
//	IMP_U32 u32DrgType;
}IMP_TgtPosition_S;


typedef struct impTgtTrajectory_S
{
	IMP_U16 u16Num;
	IMP_TgtPosition_S astPositions[IMP_MAX_TRAJECTORY_LEN];
}IMP_TgtTrajectory_S;//目标轨迹

typedef struct impTgtSubRegion_S
{
	IMP_U16 astSign[10];
	IMP_TgtTrajectory_S astSubRegionTrajectory[10];
}IMP_TgtSubRegion_S;//目标中子区域信息
	
void IMP_PushNewPosition(IMP_TgtTrajectory_S *pstTrajectory, IMP_TgtPosition_S *pstPositions)//轨迹记录
{
	int i;
	if(pstTrajectory->u16Num<IMP_MAX_TRAJECTORY_LEN) 
	{
		pstTrajectory->astPositions[pstTrajectory->u16Num] = *pstPositions;
		for(i=pstTrajectory->u16Num+1; i<IMP_MAX_TRAJECTORY_LEN; i++) pstTrajectory->astPositions[i].s32Used = 0;
		pstTrajectory->u16Num++;
	}
	else
	{
		if(!pstTrajectory->astPositions[IMP_MAX_TRAJECTORY_LEN-1].s32Used) pstTrajectory->astPositions[pstTrajectory->u16Num-1] = *pstPositions;
		else
		{
			for(i=0; i<IMP_MAX_TRAJECTORY_LEN-1; i++) pstTrajectory->astPositions[i] = pstTrajectory->astPositions[i+1];
			pstTrajectory->astPositions[IMP_MAX_TRAJECTORY_LEN-1] = *pstPositions;
		}
	}

}

typedef struct impTarget_S
{
	IMP_U8 u8Sign;
	IMP_U8 u8Used;
	IMP_U8 u8Stable;
	IMP_TgtTrajectory_S stTrajectory;
	IMP_TgtSubRegion_S stTgtSubRegion;
	IMP_U16 TargetID;
}IMP_Target_S;

typedef struct impTargetSet_S
{
	IMP_U8 u8Used;
	int frame;//记录帧数
	uchar tmpFrame[101376];//用于记录前一帧数据
	IMP_U16 TargetTotal;//目标总数
	IMP_Target_S astTarget[IMP_MAX_TGT_CNT];
}IMP_TargetSet_S;

//IMP_S32 IMP_ShowTarget(GRAY_IMAGE_S*);
IMP_S32 IMP_DeleteTarget(IMP_Target_S*, PEA_RESULT_S*);
IMP_S32 IMP_CreateTarget(IMP_Target_S*, PEA_RESULT_S*);
IMP_S32 impFindTargetOutById(IpTrackedTargetSet *pstTTS, IMP_U32 u32ID);
IMP_S32 impOutputTargetOut(IMP_Target_S *pstTarget, PEA_RESULT_S *pstResult);
double ImpGetDist(IMP_RECT_S*, IMP_RECT_S*);
void GetCentroid(GRAY_IMAGE_S*, IMP_RECT_S*, IMP_POINT_S*);

//double ColorTable[] = {0,255,0,255,0,0,0,0,255,255,255,0,255,0,255,0,255,255,0,0,0,255,255,255,128,128,128,128,128,0};

IMP_TargetSet_S stTrackTgtSet;

//算法数据存放
typedef struct impTracker_S
{
	//系统成员
	GA_HARDWARE_RS_S  *pstHwResource; //系统硬件资源
	PEA_RESULT_S      *pstResult; //系统公共数据
	
	IpTrackedTargetSet *pstOutput;
	
	//算法私有自定义成员
	IMP_S32 s32Width;
	IMP_S32 s32Height;
	//参数配置
	
}IMP_Tracker_S;


/***************************************************
name:IMP_GetMemSizeTracker
do:得到算法所需要的内存大小
input:
	pstResult:系统公共数据
output:算法需要的内存大小
history:
****************************************************/
IMP_S32 IMP_GetMemSizeTracker( PEA_RESULT_S *pstResult )
{
	IMP_S32 width = pstResult->s32Width;
	IMP_S32 height = pstResult->s32Height;
	
 	IMP_S32 size = width * height * 30;
	
	return size;
}


/***************************************************
name:IMP_CreateTracker
do:申请算法内存并始化算法参数
input:
	pstModule:算法模块
	pstResult:系统公共数据
	pstHwResource:系统硬件资源
output:
	0:失败
	非0:句柄
history:
****************************************************/
IMP_MODULE_HANDLE IMP_CreateTracker( PEA_RESULT_S *pstResult, GA_HARDWARE_RS_S *pstHwResource )
{
	IMP_MODULE_HANDLE hModule;
	int dwI;
	IMP_S32 s32Height, s32Width;
	IMP_U8 **ppu8Bkgs = 0; //max background number is 10
	MEM_MGR_ARRAY_S *pstMemMgr;
	IMP_Tracker_S *pstModule;
	IpTrackedTargetSet *pstOutput;
	s32Height = pstResult->s32Height;
	s32Width = pstResult->s32Width;
	pstOutput = &pstResult->stTrackedTargetSet; //记录输出结果地址	
	pstMemMgr = &pstHwResource->stMemMgr;
	pstModule = (IMP_Tracker_S*)IMP_MMAlloc(pstMemMgr, IMP_MEMBLK_TYPE_SLOW, sizeof(IMP_Tracker_S));	
	pstModule->pstHwResource = pstHwResource; //记录硬件信息地址
	pstModule->pstResult = pstResult; //记录系统信息地址	
	pstModule->s32Width = s32Width;
	pstModule->s32Height = s32Height;	

//	memset(&pstResult->stTrackedTargetSet, 0, sizeof(IpTrackedTargetSet));
	
	ipTrackedTargetSetClear(&pstResult->stTrackedTargetSet);

	hModule = (IMP_MODULE_HANDLE)pstModule;

	return hModule;
}


/***************************************************
name:IMP_ReleaseTracker
do:释放算法内存
input:
	hModule:算法模块句柄
output:
	0:成功
	非0:失败
history:
****************************************************/
IMP_S32 IMP_ReleaseTracker( IMP_MODULE_HANDLE hModule )
{
	int dwI;
	IMP_Tracker_S *pstModule = (IMP_Tracker_S*)hModule;
	
	IMP_S32 s32Height, s32Width;
	IMP_U8 **ppu8Bkgs = 0;
	MEM_MGR_ARRAY_S *pstMemMgr;	
	s32Width = pstModule->s32Width;
	s32Height = pstModule->s32Height;	
	pstMemMgr = &pstModule->pstHwResource->stMemMgr;

	return 0;
}


/***************************************************
name:IMP_ConfigTracker
do:配置算法参数
input:
	hModule:算法模块句柄
	pstPara:参数配置结构体
output:
	0:成功
	非0:失败
history:
****************************************************/
IMP_S32 IMP_ConfigTracker( IMP_MODULE_HANDLE hModule, IMP_TrackerPara_S *pstPara )
{
	IMP_Tracker_S *pstModule = (IMP_Tracker_S*)hModule;	
	return 0;
}


/***************************************************
name:IMP_ProcessTracker
do:算法主过程
input:
	hModule:算法模块句柄
output:
	0:成功
	非0:失败
history:
****************************************************/
IMP_S32 IMP_ProcessTracker( IMP_MODULE_HANDLE hModule )
{
	stTrackTgtSet.frame++;//记录帧数
	IMP_Tracker_S *pstModule = (IMP_Tracker_S*)hModule;
	
	GA_HARDWARE_RS_S  *pstHwResource = pstModule->pstHwResource; //系统硬件资源
	PEA_RESULT_S      *pstResult = pstModule->pstResult; //系统公共数据
	
	GRAY_IMAGE_S *pstInGray = &pstResult->stImgInGray;
	IMP_S32 s32Width = pstResult->s32Width, s32Height = pstResult->s32Height;
	
	PEA_DETECTED_REGIONSET_S *pstDRegionSet = &pstResult->stDRegionSet;

	PEA_DETECTED_REGION_S *pstDRegion = pstDRegionSet->astDrg;
	
	GRAY_IMAGE_S *pstImgBgGray = pstDRegionSet->pstImgBgGray;
	GRAY_IMAGE_S *pstImgFgOrg = pstDRegionSet->pstImgFgOrg;
	GRAY_IMAGE_S *pstImgFgRgn = pstDRegionSet->pstImgFgRgn;
	
	//the max number of region is IMP_MAX_TGT_CNT
	PEA_DETECTED_REGION_S *pstDrg = pstDRegionSet->astDrg;

//====================================================================================================跟踪
	int i, j,k;
	uchar *ptrImgCurrent = pstDRegionSet->pstImgFgRgn->pu8Data;
	uchar *ptrTmp = stTrackTgtSet.tmpFrame;
	IMP_Target_S *pstTarget = stTrackTgtSet.astTarget;
	for(i=0; i<IMP_MAX_TGT_CNT; i++)
	{
		if(pstTarget->u8Used)
		{
			IMP_RECT_S* pstRect = &pstTarget->stTrajectory.astPositions[pstTarget->stTrajectory.u16Num-1].stRg;
			IMP_TgtPosition_S stPositions;
			IMP_U8 TrackFlag = 0;
			IMP_U8 overlapSign[10]={0,0,0,0,0,0,0,0,0,0}; 
			IMP_S32 overlapPixel[10] = {1,1,1,1,1,1,1,1,1,1};
			IMP_U8 overlapFlag = 0, TargetSign = 0;
			for(j=pstRect->s16Y1; j<=pstRect->s16Y2; j++)//分析重叠
			{
				for(k=pstRect->s16X1; k<=pstRect->s16X2; k++)
				{
					if(ptrImgCurrent[j*pstInGray->s32W+k]!=0 && ptrTmp[j*pstInGray->s32W+k]!=0)
					{
						if(overlapFlag==0) { overlapSign[0] = ptrImgCurrent[j*pstInGray->s32W+k]; overlapFlag++; }
						else if(ptrImgCurrent[j*pstInGray->s32W+k]==overlapSign[0]) overlapPixel[0]++;
						else if(overlapFlag==1) { overlapSign[1] = ptrImgCurrent[j*pstInGray->s32W+k]; overlapFlag++; }
						else if(ptrImgCurrent[j*pstInGray->s32W+k]==overlapSign[1]) overlapPixel[1]++;
						else if(overlapFlag==2) { overlapSign[2] = ptrImgCurrent[j*pstInGray->s32W+k]; overlapFlag++; }
						else if(ptrImgCurrent[j*pstInGray->s32W+k]==overlapSign[2]) overlapPixel[2]++;
						else if(overlapFlag==3) { overlapSign[3] = ptrImgCurrent[j*pstInGray->s32W+k]; overlapFlag++; }
						else if(ptrImgCurrent[j*pstInGray->s32W+k]==overlapSign[3]) overlapPixel[3]++;
						else if(overlapFlag==4) { overlapSign[4] = ptrImgCurrent[j*pstInGray->s32W+k]; overlapFlag++; }
						else if(ptrImgCurrent[j*pstInGray->s32W+k]==overlapSign[4]) overlapPixel[4]++;
						else if(overlapFlag==5) { overlapSign[5] = ptrImgCurrent[j*pstInGray->s32W+k]; overlapFlag++; }
						else if(ptrImgCurrent[j*pstInGray->s32W+k]==overlapSign[5]) overlapPixel[5]++;
						else if(overlapFlag==6) { overlapSign[6] = ptrImgCurrent[j*pstInGray->s32W+k]; overlapFlag++; }
						else if(ptrImgCurrent[j*pstInGray->s32W+k]==overlapSign[6]) overlapPixel[6]++;
						else if(overlapFlag==7) { overlapSign[7] = ptrImgCurrent[j*pstInGray->s32W+k]; overlapFlag++; }
						else if(ptrImgCurrent[j*pstInGray->s32W+k]==overlapSign[7]) overlapPixel[7]++;
						else if(overlapFlag==8) { overlapSign[8] = ptrImgCurrent[j*pstInGray->s32W+k]; overlapFlag++; }
						else if(ptrImgCurrent[j*pstInGray->s32W+k]==overlapSign[8]) overlapPixel[8]++;
						else if(overlapFlag==9) { overlapSign[9] = ptrImgCurrent[j*pstInGray->s32W+k]; overlapFlag++; }
						else if(ptrImgCurrent[j*pstInGray->s32W+k]==overlapSign[9]) overlapPixel[9]++; //ptrImgCurrent[j*pstInGray->s32W+k] = TargetSign;
					}
//					else if(ptrImgCurrent[j*pstInGray->s32W+k]==overlapSign[0]||ptrImgCurrent[j*pstInGray->s32W+k]==overlapSign[1]||ptrImgCurrent[j*pstInGray->s32W+k]==overlapSign[2]||ptrImgCurrent[j*pstInGray->s32W+k]==overlapSign[3]||ptrImgCurrent[j*pstInGray->s32W+k]==overlapSign[4]||ptrImgCurrent[j*pstInGray->s32W+k]==overlapSign[5]||ptrImgCurrent[j*pstInGray->s32W+k]==overlapSign[6]||ptrImgCurrent[j*pstInGray->s32W+k]==overlapSign[7]||ptrImgCurrent[j*pstInGray->s32W+k]==overlapSign[8]||ptrImgCurrent[j*pstInGray->s32W+k]==overlapSign[9]) ptrImgCurrent[j*pstInGray->s32W+k] = TargetSign;
				}
			}

			int n; j = 0;//矩形框分析
			typedef struct
			{
				IMP_U8 u8Used;
				IMP_RECT_S Rect;
				IMP_U32 Pixels[10];
				IMP_U8 Signs[10];
			}RectTmp_S;
			RectTmp_S RectTmp[10];
			while(overlapFlag>0)
			{
				pstDrg = pstDRegionSet->astDrg;
				for(n=0; n<IMP_MAX_TGT_CNT; n++)
				{
					if(pstDrg->u8Used && pstDrg->u8Sign==overlapSign[overlapFlag-1])
					{
						RectTmp[j].u8Used = 1;
						RectTmp[j].Rect = pstDrg->stRect; 
						RectTmp[j].Pixels[0] = (IMP_U32)pstDrg->s32AreaPixel;
						RectTmp[j].Signs[0] = pstDrg->u8Sign;
						for(k=1; k<10; k++)
						{
							RectTmp[j].Pixels[k] = 0;
							RectTmp[j].Signs[k] = 0;
						}	
						TrackFlag = 1; 
						j++;
					}
					pstDrg++;
				}
				overlapFlag--;
			}
			while(j<10) 
			{
				RectTmp[j].u8Used = 0;
				for(k=0; k<10; k++)
				{
					RectTmp[j].Pixels[k] = 0;
					RectTmp[j].Signs[k] = 0;
				}
				j++;
			}

			IMP_U8 flag = 1;
			while(flag && TrackFlag)
			{
				k = 1;
				flag = 0;
				for(j=1; j<10; j++)
					for(n=0; n<k; n++)
					{
						if(RectTmp[j].u8Used)
						{
							if(ImpGetDist(&RectTmp[j].Rect, &RectTmp[n].Rect)<10)//矩形框距离
							{
								flag = 1;
								if(RectTmp[j].Rect.s16X1<RectTmp[n].Rect.s16X1) RectTmp[n].Rect.s16X1 = RectTmp[j].Rect.s16X1;
								if(RectTmp[j].Rect.s16Y1<RectTmp[n].Rect.s16Y1) RectTmp[n].Rect.s16Y1 = RectTmp[j].Rect.s16Y1;
								if(RectTmp[j].Rect.s16X2>RectTmp[n].Rect.s16X2) RectTmp[n].Rect.s16X2 = RectTmp[j].Rect.s16X2;
								if(RectTmp[j].Rect.s16Y2>RectTmp[n].Rect.s16Y2) RectTmp[n].Rect.s16Y2 = RectTmp[j].Rect.s16Y2;
								RectTmp[j].u8Used = 0;
								int p, q = 0;
								for(p=0; p<10; p++)
								{
									if(RectTmp[n].Pixels[p]==0)
									{
										if(RectTmp[j].Pixels[q]!=0) { RectTmp[n].Pixels[p] = RectTmp[j].Pixels[q]; RectTmp[j].Pixels[q] = 0; }
										q++;
									}
								}
								q = 0;
								for(p=0; p<10; p++)
								{
									if(RectTmp[n].Signs[p]==0)
									{
										if(RectTmp[j].Signs[q]!=0) { RectTmp[n].Signs[p] = RectTmp[j].Signs[q];  RectTmp[j].Signs[q] = 0; }
										q++;
									}
								}
							}
							else
							{
								RectTmp[k] = RectTmp[j];
								k++;
								break;
							}
						}
					}
			}

			if(!TrackFlag)
			{ 
				pstTarget->u8Used = 0;
				IMP_DeleteTarget(pstTarget, pstResult);
			}
			else
			{
				n = 0;
				IMP_POINT_S stPt1;
				IMP_POINT_S stPt2 = pstTarget->stTrajectory.astPositions[pstTarget->stTrajectory.u16Num-1].stPt;
				GetCentroid(pstImgFgRgn, &RectTmp[0].Rect, &stPt1);
				double dist = pow(stPt1.s16X-stPt2.s16X, 2)+pow(stPt1.s16Y-stPt2.s16Y, 2);
				double dist0 = 0;
				for(k=1; k<10; k++)
				{
					if(RectTmp[k].u8Used)
					{
						GetCentroid(pstImgFgRgn, &RectTmp[k].Rect, &stPt1);
						dist0 = pow(stPt1.s16X-stPt2.s16X, 2)+pow(stPt1.s16Y-stPt2.s16Y, 2);
						if(dist0<dist){ n = k; dist = dist0; };
					}
				}
				stPositions.stRg = RectTmp[n].Rect;
				
				for(k=0; RectTmp[n].Signs[k]!=0; k++)
				{
					pstDrg = pstDRegionSet->astDrg;
					for(j=0; j<IMP_MAX_TGT_CNT; j++)
					{
						if(pstDrg->u8Used && pstDrg->u8Sign==RectTmp[n].Signs[k]) { pstDrg->u8Used = 0; break; }
						pstDrg++;
					}		
				}
				stPositions.u32AreaPixel = 0;
				stPositions.s32Used = 1;
				GetCentroid(pstImgFgRgn, &stPositions.stRg, &stPositions.stPt);
				if(pstTarget->stTrajectory.u16Num>=IMP_STABLE_THRESHOLD) 
				{
					if(pstTarget->u8Stable==0)
					{
						pstTarget->u8Stable = 1;
					}
					else//计算速度和方向
					{
						int n;
						IMP_S32 SumX = 0;
						IMP_S32 SumY = 0;
						for(n=0; n<16; n++)
						{
							SumX += (IMP_S32)(pstTarget->stTrajectory.astPositions[n].stPt.s16X-pstTarget->stTrajectory.astPositions[n+1].stPt.s16X);
							SumY += (IMP_S32)(pstTarget->stTrajectory.astPositions[n].stPt.s16Y-pstTarget->stTrajectory.astPositions[n+1].stPt.s16Y);
						}
						double VX = SumX/16.0;
						double VY = SumY/16.0;
						stPositions.s32Velocity = (IMP_S32)(sqrt(pow(VX,2)+pow(VY,2))*128);
						stPositions.s32Direction = (IMP_S32)(atan2(VY,VX)*180.0/3.1415926);
					}
				}
				else
				{
					stPositions.s32Velocity = 0;
					stPositions.s32Direction = 0;
				}
				IMP_PushNewPosition(&pstTarget->stTrajectory, &stPositions);
			}
		}
		pstTarget++;
	}

//======================================================================================================加入新目标
	i = 0; j = 0;
	pstDrg = pstDRegionSet->astDrg;
	pstTarget = stTrackTgtSet.astTarget;
	IMP_POINT_S TrackPoint;
	while(i<IMP_MAX_TGT_CNT)
	{
		if(pstDrg->u8Used && pstDrg->s32AreaPixel>=IMP_MIN_TGT_AREA)
		{
			while(j<IMP_MAX_TGT_CNT)
			{
				if(!pstTarget->u8Used)
				{
					pstTarget->TargetID = stTrackTgtSet.TargetTotal;
					stTrackTgtSet.TargetTotal++;
					pstTarget->u8Used = 1;
					pstTarget->u8Sign = pstDrg->u8Sign;
					pstTarget->u8Stable = 0;
					pstTarget->stTrajectory.u16Num = 0;
					IMP_TgtPosition_S stPositions;
					stPositions.s32Used = 1;
					stPositions.stRg = pstDrg->stRect;
					GetCentroid(pstImgFgRgn, &stPositions.stRg, &stPositions.stPt);
					stPositions.u32AreaPixel = (IMP_U32)pstDrg->s32AreaPixel;
					stPositions.s32Velocity = 0;
					stPositions.s32Direction = 0;
					IMP_PushNewPosition(&pstTarget->stTrajectory, &stPositions);
					IMP_CreateTarget(pstTarget, pstResult);
					break;
				}
				pstTarget++;
				j++;
			}
		}
		pstDrg++;
		i++;
	}

		/*if(frame>1298&&frame<1400)
		{
		IplImage *pstImgForShow = cvCreateImage(cvSize((int)pstImgFgOrg->s32W,(int)pstImgFgOrg->s32H),IPL_DEPTH_8U,1);
		uchar *ptrData = tmpFrame;
		int i,j;
		for(i=0; i<pstImgForShow->height; i++)
		{
			uchar *ptr_img = (uchar*)(pstImgForShow->imageData+i*pstImgForShow->widthStep);
			for(j=0; j<pstImgForShow->width; j++)
			{
				if(*ptrData>0) ptr_img[j] = 255;
				else ptr_img[j] = 0;
				ptrData++;
			}
		}
		char tmp[10];
		sprintf(tmp,"%d.jpg",frame);
		cvSaveImage(tmp,pstImgForShow,0);
		}*/

	i = 0;
	uchar *ptrData = (uchar*)pstImgFgRgn->pu8Data;
	while(i<s32Width*s32Height){ stTrackTgtSet.tmpFrame[i] = ptrData[i]; i++; }

//=============================================================================================结果保存
	impOutputTargetOut(stTrackTgtSet.astTarget, pstResult);

//printf("output target number:%d\n", Total);
//====================================================================================================结果显示
#if 0
//	cvNamedWindow("test",1);
	IMP_ShowTarget(pstInGray);
//	cvMoveWindow("test", 360, 360);
	
	ipShowGrayImage(s32Width, s32Height, pstInGray->pu8Data, "tracker_inframe");
	ipShowGrayImage(s32Width, s32Height, pstImgBgGray->pu8Data, "tracker_bkg");
	ipShowBinImage(s32Width, s32Height, pstImgFgOrg->pu8Data, "tracker_frg");
//	ipShowBinImage(s32Width, s32Height, pstImgFgRgn->pu8Data, "tracker_rgn");
	
//	cvMoveWindow("tracker_inframe", 0, 0);
//	cvMoveWindow("tracker_bkg", 360, 0);
//	cvMoveWindow("tracker_frg", 0, 360);
//	cvMoveWindow("tracker_rgn", 0, 720);
#endif

	return 0;
}

void GetCentroid(GRAY_IMAGE_S *pstImg, IMP_RECT_S *pstRect, IMP_POINT_S *pstCentroid)
{
	uchar *ptrImg = pstImg->pu8Data;
	int j,k;
	int X = 0; int Y = 0; int i = 0;
	for(j=pstRect->s16Y1; j<=pstRect->s16Y2; j++)//分析重叠
	{
		for(k=pstRect->s16X1; k<=pstRect->s16X2; k++)
		{
			if(ptrImg[j*pstImg->s32W+k]!=0)
			{
				X += k;
				Y += j;
				i++;
			}
		}
	}
	pstCentroid->s16X = (IMP_S16)((double)X/(double)i);
	pstCentroid->s16Y = (IMP_S16)((double)Y/(double)i);
}

double ImpGetDist(IMP_RECT_S *pstRect1, IMP_RECT_S *pstRect2)
{
	double dist = 0;
	if(pstRect1->s16X2<pstRect2->s16X1)
	{
		if(pstRect1->s16Y2<pstRect2->s16Y1) dist = sqrt(pow(pstRect2->s16X1-pstRect1->s16X2,2)+pow(pstRect2->s16Y1-pstRect1->s16Y2,2));
		else 
		{
			if(pstRect1->s16Y1>pstRect2->s16Y2) dist = sqrt(pow(pstRect2->s16X1-pstRect1->s16X2,2)+pow(pstRect1->s16Y1-pstRect2->s16Y2,2));
			else dist = pstRect2->s16X1-pstRect1->s16X2;
		}
	}
	else
	{
		if(pstRect1->s16X1>pstRect2->s16X2)
		{
			if(pstRect1->s16Y2<pstRect2->s16Y1) dist = sqrt(pow(pstRect1->s16X1-pstRect2->s16X2,2)+pow(pstRect2->s16Y1-pstRect1->s16Y2,2));
			else 
			{
				if(pstRect1->s16Y1>pstRect2->s16Y2) dist = sqrt(pow(pstRect1->s16X1-pstRect2->s16X2,2)+pow(pstRect1->s16Y1-pstRect2->s16Y2,2));
				else dist = pstRect1->s16X1-pstRect2->s16X2;
			}
		}
		else
		{
			if(pstRect1->s16Y2<pstRect2->s16Y1) dist = pstRect2->s16Y1-pstRect1->s16Y2;
			else
			{
				if(pstRect1->s16Y1>pstRect2->s16Y2) dist = pstRect1->s16Y1-pstRect2->s16Y2;
			}
		}
	}
	return dist;	
}
IMP_S32 impFindTargetOutById(IpTrackedTargetSet *pstTTS, IMP_U32 u32ID)
{
	IMP_S32 s32TI;
	IpTrackedTarget *pstTargetOut = 0;
	
	for (s32TI = 1; s32TI < IMP_MAX_TGT_CNT; s32TI++)
	{
		pstTargetOut = &pstTTS->astTargets[s32TI];
		if (pstTargetOut->s32Used && pstTargetOut->u32TargetId == u32ID)
		{
			return s32TI;
		}
	}
	
	return 0; //cannot find object
}


IMP_S32 impOutputTargetOut(IMP_Target_S *pstTarget, PEA_RESULT_S *pstResult)
{
	IpTrackedTarget *pstTargetOut = 0;
	IpTrackedTarget *pstTargets = pstResult->stTrackedTargetSet.astTargets;
	PEA_DETECTED_REGIONSET_S *pstDRegionSet = &pstResult->stDRegionSet;
	IMP_Target_S *pstTgts = stTrackTgtSet.astTarget;
    IMP_S32 i = 0, j = 0; 
    IMP_S32 Total = 0;
	while(i<IMP_MAX_TGT_CNT)
	{
		if(pstTgts->u8Used)
		{
			IMP_S32 s32TI = impFindTargetOutById(&pstResult->stTrackedTargetSet, (IMP_U32)pstTgts->TargetID);
			if (s32TI == 0) continue; //cannot find object
			pstTargetOut = &pstTargets[s32TI];
		//	pstTargets->s32Used = 1;
			if(pstTgts->u8Stable) 
			{
				IMP_SET_TGT_STABLE(pstTargetOut->stTargetInfo.u32Status);
				IMP_SET_TGT_MEASURED(pstTargetOut->stTargetInfo.u32Status);
				IMP_SET_TGT_MTREND(pstTargetOut->stTargetInfo.u32Status);
			}
		//	pstTargets->u32TargetId = (IMP_U32)pstTgts->TargetID;
			pstTargetOut->stTargetImage.pstDRegions = pstDRegionSet;
			pstTargetOut->stTargetImage.s32DrgIndex = (IMP_S32)pstTgts->u8Sign;
			for(j=0; j<pstTgts->stTrajectory.u16Num; j++)
			{
				pstTargetOut->stTrajectory.astPositions[j].s32Used = pstTgts->stTrajectory.astPositions[j].s32Used;
				pstTargetOut->stTrajectory.astPositions[j].stPt = pstTgts->stTrajectory.astPositions[j].stPt;
				pstTargetOut->stTrajectory.astPositions[j].stRg = pstTgts->stTrajectory.astPositions[j].stRg;
				pstTargetOut->stTrajectory.astPositions[j].u32AreaPixel = pstTgts->stTrajectory.astPositions[j].u32AreaPixel;
				pstTargetOut->stTrajectory.astPositions[j].s32Velocity = pstTgts->stTrajectory.astPositions[j].s32Velocity;
				pstTargetOut->stTrajectory.astPositions[j].s32Direction = pstTgts->stTrajectory.astPositions[j].s32Direction;
			}
			pstTargetOut->stTrajectory.s32Num = pstTgts->stTrajectory.u16Num;
			Total++;
		}
		pstTgts++;
		i++;
	}
	pstResult->stTrackedTargetSet.s32UsedTotal = Total;
	
	return 0;
}


IMP_S32 IMP_DeleteTarget(IMP_Target_S *pstTarget, PEA_RESULT_S *pstResult)
{
	IMP_S32 s32TI;
	
	s32TI = impFindTargetOutById(&pstResult->stTrackedTargetSet, (IMP_U32)pstTarget->TargetID);
	if (s32TI == 0) return -1; //cannot find object
	
	ipTrackedTargetSetFree(&pstResult->stTrackedTargetSet, s32TI);
	
	return 0;
}

IMP_S32 IMP_CreateTarget(IMP_Target_S *pstTarget, PEA_RESULT_S *pstResult)
{
	IMP_S32 s32TI;
	IpTrackedTarget *pstTargetOut = 0;
	
	s32TI = ipTrackedTargetSetAlloc(&pstResult->stTrackedTargetSet);
	if (s32TI == 0) return -1; //cannot alloc memory for object
	
	pstTargetOut = &pstResult->stTrackedTargetSet.astTargets[s32TI];
	pstTargetOut->s32Used = 1;
	pstTargetOut->u32TargetId = (IMP_U32)pstTarget->TargetID;
//	printf("-----------------------TargetID:%d\n", pstTargetOut->u32TargetId);
	return 0;
}

#if 0
IMP_S32 IMP_ShowTarget(GRAY_IMAGE_S *pstGrayImg)
{	
	IplImage *pstImgForShow = cvCreateImage(cvSize((int)pstGrayImg->s32W,(int)pstGrayImg->s32H),IPL_DEPTH_8U,3);
	uchar *ptrGrayData = (uchar*)pstGrayImg->pu8Data;

	int i,j;
	for(i=0; i<pstImgForShow->height; i++)
	{
		uchar *ptr_img = (uchar*)(pstImgForShow->imageData+i*pstImgForShow->widthStep);
		for(j=0; j<pstImgForShow->width; j++)
		{
			ptr_img[3*j] = *ptrGrayData;
			ptr_img[3*j+1] = *ptrGrayData;
			ptr_img[3*j+2] = *ptrGrayData;
//			if(*ptrGrayData)
//			{
//				ptr_img[3*j] = 255;
//				ptr_img[3*j+1] = 255;
//				ptr_img[3*j+2] = 255;
//			}
//			else
//			{
//				ptr_img[3*j] = 0;
//				ptr_img[3*j+1] = 0;
//				ptr_img[3*j+2] = 0;
//			}
			ptrGrayData++;
		}
	}

	IMP_Target_S *pstTarget = stTrackTgtSet.astTarget;
	IMP_RECT_S *pstRect;
	CvFont font;  
	cvInitFont(&font, 0, 0.5, 0.5, 0, 1, 8); 
       i = 0;
	while(i<10)
	{
		if(pstTarget->u8Used && pstTarget->u8Stable)
		{
			pstRect = &pstTarget->stTrajectory.astPositions[pstTarget->stTrajectory.u16Num-1].stRg;
			cvRectangle(pstImgForShow,cvPoint((int)pstRect->s16X1,(int)pstRect->s16Y1),cvPoint((int)pstRect->s16X2,(int)pstRect->s16Y2),cvScalar(ColorTable[i*3],ColorTable[i*3+1],ColorTable[i*3+2],0),1,8,0);

			for(j=0; j<pstTarget->stTrajectory.u16Num; j++)
			{
				IMP_S16 X = pstTarget->stTrajectory.astPositions[j].stPt.s16X;
				IMP_S16 Y = pstTarget->stTrajectory.astPositions[j].stPt.s16Y;
				pstImgForShow->imageData[Y*pstImgForShow->widthStep + 3*X] = ColorTable[i*3];
				pstImgForShow->imageData[Y*pstImgForShow->widthStep + 3*X + 1] = ColorTable[i*3+1];
				pstImgForShow->imageData[Y*pstImgForShow->widthStep + 3*X + 2] = ColorTable[i*3+2];
			}


			char tmp1[10];
			sprintf(tmp1,"%d",pstTarget->TargetID); 
			cvPutText(pstImgForShow, tmp1, cvPoint((int)pstRect->s16X1,(int)pstRect->s16Y2), &font, cvScalar(ColorTable[i*3],ColorTable[i*3+1],ColorTable[i*3+2],0));
//			char tmp4[10];
//			sprintf(tmp4,"%d",pstTarget->stTrajectory.astPositions[0].s32Velocity); 
//			cvPutText(pstImgForShow, tmp4, cvPoint((int)pstRect->s16X1,(int)(pstRect->s16Y2-20)), &font, cvScalar(ColorTable[i*3],ColorTable[i*3+1],ColorTable[i*3+2],0));
//			char tmp5[10];
//			sprintf(tmp5,"%d",pstTarget->stTrajectory.astPositions[0].s32Direction); 
//			cvPutText(pstImgForShow, tmp5, cvPoint((int)pstRect->s16X1,(int)pstRect->s16Y2-40), &font, cvScalar(ColorTable[i*3],ColorTable[i*3+1],ColorTable[i*3+2],0));
		}
		pstTarget++;
		i++;
	}

	char tmp2[10];
	sprintf(tmp2,"frame:%d",stTrackTgtSet.frame);
	cvPutText(pstImgForShow, tmp2, cvPoint(240,20), &font, cvScalar(0,255,0,0));
	cvShowImage("test",pstImgForShow);

//	char tmp3[10];
//	sprintf(tmp3,"%d.jpg",stTrackTgtSet.frame);
//	if(stTrackTgtSet.frame>4900 && stTrackTgtSet.frame<5000)	cvSaveImage(tmp3,pstImgForShow,0);

	cvReleaseImage(&pstImgForShow);

	return 0;
}
#endif

















