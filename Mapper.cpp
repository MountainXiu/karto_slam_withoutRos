/*
 * Copyright 2010 SRI International
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <sstream>
#include <fstream>
#include <stdexcept>
#include <queue>
#include <set>
#include <list>
#include <iterator>

#include <math.h>
#include <assert.h>

#include "Mapper.h"
#include "Karto.h"
#include "Macros.h"
#include "Math.h"
#include "Types.h"

#include <opencv2/opencv.hpp>



namespace karto
{

  // enable this for verbose debug information
  // #define KARTO_DEBUG

  #define MAX_VARIANCE            500.0
  #define DISTANCE_PENALTY_GAIN   0.2
  #define ANGLE_PENALTY_GAIN      0.2

  ////////////////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////////////

  /**
   * Manages the scan data for a device  扫描设备管理类
   */
  class ScanManager
  {
  public:
    /**
     * Default constructor
     */
    ScanManager(kt_int32u runningBufferMaximumSize, kt_double runningBufferMaximumDistance)
      : m_pLastScan(NULL)
      , m_RunningBufferMaximumSize(runningBufferMaximumSize)
      , m_RunningBufferMaximumDistance(runningBufferMaximumDistance)
    {
    }

    /**
     * Destructor
     */
    virtual ~ScanManager()
    {
      Clear();
    }

  public:
    /**
     * Adds scan to vector of processed scans tagging scan with given unique id
     * @param pScan 注意与 AddRunningScan 的区别
     */
    inline void AddScan(LocalizedRangeScan* pScan, kt_int32s uniqueId)
    {
      // assign state id to scan
      pScan->SetStateId(static_cast<kt_int32u>(m_Scans.size()));

      // assign unique id to scan
      pScan->SetUniqueId(uniqueId);

      // add it to scan buffer
      m_Scans.push_back(pScan);
    }

    /**
     * Gets last scan 获取给定传感器名称的最新数据
     * @param deviceId
     * @return last localized range scan
     */
    inline LocalizedRangeScan* GetLastScan()
    {
      return m_pLastScan;
    }

    /**
     * Sets the last scan  设置给定传感器名称的最新数据
     * @param pScan
     */
    inline void SetLastScan(LocalizedRangeScan* pScan)
    {
      m_pLastScan = pScan;
    }

    /**
     * Gets scans
     * @return scans
     */
    inline LocalizedRangeScanVector& GetScans()
    {
      return m_Scans;
    }

    /**
     * Gets running scans
     * @return running scans
     */
    inline LocalizedRangeScanVector& GetRunningScans()
    {
      return m_RunningScans;
    }

    /**
     * Adds scan to vector of running scans 在 m_RunningScans 中增加一个 scan 数据，并且总数和距离都有限制
     * @param pScan 注意与  AddScan 的区别
     */
    void AddRunningScan(LocalizedRangeScan* pScan)
    {
      m_RunningScans.push_back(pScan);

      // vector has at least one element (first line of this function), so this is valid
      Pose2 frontScanPose = m_RunningScans.front()->GetSensorPose();
      Pose2 backScanPose = m_RunningScans.back()->GetSensorPose();

      // cap vector size and remove all scans from front of vector that are too far from end of vector
      kt_double squaredDistance = frontScanPose.GetPosition().SquaredDistance(backScanPose.GetPosition());
      while (m_RunningScans.size() > m_RunningBufferMaximumSize ||
             squaredDistance > math::Square(m_RunningBufferMaximumDistance) - KT_TOLERANCE)
      {
        // remove front of running scans
        m_RunningScans.erase(m_RunningScans.begin());

        // recompute stats of running scans
        frontScanPose = m_RunningScans.front()->GetSensorPose();
        backScanPose = m_RunningScans.back()->GetSensorPose();
        squaredDistance = frontScanPose.GetPosition().SquaredDistance(backScanPose.GetPosition());
      }
    }

    /**
     * Deletes data of this buffered device 删除所有的缓冲区数据
     */
    void Clear()
    {
      m_Scans.clear();
      m_RunningScans.clear();
    }

  private:
    LocalizedRangeScanVector m_Scans;
    LocalizedRangeScanVector m_RunningScans;
    LocalizedRangeScan* m_pLastScan;

    kt_int32u m_RunningBufferMaximumSize;
    kt_double m_RunningBufferMaximumDistance;
  };  // ScanManager

  ////////////////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////////////
  /* 注册传感器 */
  void MapperSensorManager::RegisterSensor(const Name& rSensorName)
  {
    if (GetScanManager(rSensorName) == NULL)
    {
      m_ScanManagers[rSensorName] = new ScanManager(m_RunningBufferMaximumSize, m_RunningBufferMaximumDistance);
    }
  }


  /**
   * Gets scan from given device with given ID 根据传感器名称和ID号获取扫描数据
   * @param rSensorName
   * @param scanNum
   * @return localized range scan
   */
  LocalizedRangeScan* MapperSensorManager::GetScan(const Name& rSensorName, kt_int32s scanIndex)
  {
    ScanManager* pScanManager = GetScanManager(rSensorName);
    if (pScanManager != NULL)
    {
      return pScanManager->GetScans().at(scanIndex);
    }

    assert(false);
    return NULL;
  }

  /**
   * Gets last scan of given device
   * @param pLaserRangeFinder
   * @return last localized range scan of device
   */
  inline LocalizedRangeScan* MapperSensorManager::GetLastScan(const Name& rSensorName)
  {
    RegisterSensor(rSensorName);

    return GetScanManager(rSensorName)->GetLastScan();
  }

  /**
   * Sets the last scan of device of given scan
   * @param pScan
   */
  inline void MapperSensorManager::SetLastScan(LocalizedRangeScan* pScan)
  {
    GetScanManager(pScan)->SetLastScan(pScan);
  }

  /**
   * Adds scan to scan vector of device that recorded scan 增加一个 scan 数据
   * @param pScan
   */
  void MapperSensorManager::AddScan(LocalizedRangeScan* pScan)
  {
    GetScanManager(pScan)->AddScan(pScan, m_NextScanId);
    m_Scans.push_back(pScan);
    m_NextScanId++;
  }

  /**
   * Adds scan to running scans of device that recorded scan
   * @param pScan
   */

  void MapperSensorManager::AddRunningScan(LocalizedRangeScan* pScan) // changed by myself, delete later
  //inline void MapperSensorManager::AddRunningScan(LocalizedRangeScan* pScan)
  {
    GetScanManager(pScan)->AddRunningScan(pScan);
  }

  /**
   * Gets scans of device
   * @param rSensorName
   * @return scans of device
   */
  inline LocalizedRangeScanVector& MapperSensorManager::GetScans(const Name& rSensorName)
  {
    return GetScanManager(rSensorName)->GetScans();
  }

  /**
   * Gets running scans of device
   * @param rSensorName
   * @return running scans of device
   */
  LocalizedRangeScanVector& MapperSensorManager::GetRunningScans(const Name& rSensorName) // changed by myself, delete later
  //inline LocalizedRangeScanVector& MapperSensorManager::GetRunningScans(const Name& rSensorName)
  {
    return GetScanManager(rSensorName)->GetRunningScans();
  }

  /**
   * Gets all scans of all devices 获取所有传感器的所有数据
   * @return all scans of all devices
   */
  LocalizedRangeScanVector MapperSensorManager::GetAllScans()
  {
    LocalizedRangeScanVector scans;

	/* 传感器的循环 */
	forEachMe(ScanManagerMap, &m_ScanManagers)
    {
      LocalizedRangeScanVector& rScans = iter->second->GetScans();

	  /* 将所有的数据插入到 scans 中 */
      scans.insert(scans.end(), rScans.begin(), rScans.end());
    }

    return scans;
  }

  /**
   * Deletes all scan managers of all devices
   */
  void MapperSensorManager::Clear()
  {
//    SensorManager::Clear();

	  forEachMe(ScanManagerMap, &m_ScanManagers)
    {
      delete iter->second;
    }

    m_ScanManagers.clear();
  }

  ////////////////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////////////

  ScanMatcher::~ScanMatcher()
  {
    delete m_pCorrelationGrid;
    delete m_pSearchSpaceProbs;
    delete m_pGridLookup;
  }

  /* 创建一个扫描匹配器 */
  /*
  Mapper* pMapper,             扫描匹配的地图信息
  kt_double searchSize,        扫描匹配的搜索窗口大小
  kt_double resolution,        扫描匹配的搜索窗口分辨率
  kt_double smearDeviation,    扫描匹配的搜索平滑窗口大小
  kt_double rangeThreshold     激光雷达的范围
  */
  ScanMatcher* ScanMatcher::Create(Mapper* pMapper, kt_double searchSize, kt_double resolution,
                                   kt_double smearDeviation, kt_double rangeThreshold)
  {
    // invalid parameters
    if (resolution <= 0)
    {
      return NULL;
    }
    if (searchSize <= 0)
    {
      return NULL;
    }
    if (smearDeviation < 0)
    {
      return NULL;
    }
    if (rangeThreshold <= 0)
    {
      return NULL;
    }

    assert(math::DoubleEqual(math::Round(searchSize / resolution), (searchSize / resolution)));

    // calculate search space in grid coordinates 计算搜索空间
    kt_int32u searchSpaceSideSize = static_cast<kt_int32u>(math::Round(searchSize / resolution) + 1);

    // compute requisite size of correlation grid (pad grid so that scan points can't fall off the grid
    // if a scan is on the border of the search space) 如果在搜索空间的边缘，计算需要 pad 的网格大小
    kt_int32u pointReadingMargin = static_cast<kt_int32u>(ceil(rangeThreshold / resolution));

    kt_int32s gridSize = searchSpaceSideSize + 2 * pointReadingMargin;

    // create correlation grid 创建相关性网格
    assert(gridSize % 2 == 1); /* 必须为奇数，当前点刚好位于中心 */
    CorrelationGrid* pCorrelationGrid = CorrelationGrid::CreateGrid(gridSize, gridSize, resolution, smearDeviation);

    // create search space probabilities 创建搜索空间概率表
    Grid<kt_double>* pSearchSpaceProbs = Grid<kt_double>::CreateGrid(searchSpaceSideSize,searchSpaceSideSize, resolution);

    ScanMatcher* pScanMatcher = new ScanMatcher(pMapper);
    pScanMatcher->m_pCorrelationGrid = pCorrelationGrid;
    pScanMatcher->m_pSearchSpaceProbs = pSearchSpaceProbs;
    pScanMatcher->m_pGridLookup = new GridIndexLookup<kt_int8u>(pCorrelationGrid);

    return pScanMatcher;
  }

  /**
   * Match given scan against set of scans
   * @param pScan scan being scan-matched
   * @param rBaseScans set of scans whose points will mark cells in grid as being occupied
   * @param rMean output parameter of mean (best pose) of match
   * @param rCovariance output parameter of covariance of match
   * @param doPenalize whether to penalize matches further from the search center
   * @param doRefineMatch whether to do finer-grained matching if coarse match is good (default is true)
   * @return strength of response
   */
  /*
  功能：扫描匹配,这是扫描匹配的核心函数
  pScan： 当前的激光扫描 
  rBaseScans：以前所有的激光扫描
  rMean：输出最佳匹配位置
  rCovariance：协方差矩阵
  doPenalize：是否惩罚远离搜索中心的位置
  doRefineMatch：是否做精细的匹配
  */
  kt_double ScanMatcher::MatchScan(LocalizedRangeScan* pScan, const LocalizedRangeScanVector& rBaseScans, Pose2& rMean,
                                   Matrix3& rCovariance, kt_bool doPenalize, kt_bool doRefineMatch)
  {
    ///////////////////////////////////////
    // set scan pose to be center of grid

    // 1. get scan position，得到扫描器的位置坐标,这里的坐标系是世界坐标系
    Pose2 scanPose = pScan->GetSensorPose();

    // scan has no readings; cannot do scan matching
    // best guess of pose is based off of adjusted odometer reading
	// 如果没有数据，则不做扫描匹配
    if (pScan->GetNumberOfRangeReadings() == 0)
    {
      rMean = scanPose; // 直接返回一个机器人位置

      // maximum covariance 设置协方差矩阵
      rCovariance(0, 0) = MAX_VARIANCE;  // XX
      rCovariance(1, 1) = MAX_VARIANCE;  // YY
      rCovariance(2, 2) = 4 * math::Square(m_pMapper->m_pCoarseAngleResolution->GetValue());  // TH*TH

      return 0.0;
    }

    // 2. get size of grid 得到 ROI 的大小, m_pCorrelationGrid 这个是由匹配参数确定的，在Create函数中建立
    Rectangle2<kt_int32s> roi = m_pCorrelationGrid->GetROI();
    // 3. compute offset (in meters - lower left corner) 计算偏移量, 就是扫描位置相对于相关性网格中心的偏移量
    Vector2<kt_double> offset;
    offset.SetX(scanPose.GetX() - (0.5 * (roi.GetWidth()  - 1) * m_pCorrelationGrid->GetResolution()));
    offset.SetY(scanPose.GetY() - (0.5 * (roi.GetHeight() - 1) * m_pCorrelationGrid->GetResolution()));

    // 4. set offset  设置偏移量
    m_pCorrelationGrid->GetCoordinateConverter()->SetOffset(offset);

    ///////////////////////////////////////

    // set up correlation grid 建立相关性网格
    AddScans(rBaseScans, scanPose.GetPosition());

    // compute how far to search in each direction
	/* 确定搜索范围 */
    Vector2<kt_double> searchDimensions(m_pSearchSpaceProbs->GetWidth(), m_pSearchSpaceProbs->GetHeight());
	/* 确定搜索范围偏移量 */
	Vector2<kt_double> coarseSearchOffset(0.5 * (searchDimensions.GetX() - 1) * m_pCorrelationGrid->GetResolution(),
                                          0.5 * (searchDimensions.GetY() - 1) * m_pCorrelationGrid->GetResolution());

    // a coarse search only checks half the cells in each dimension
	/* 粗搜索时的搜索分辨率 */
	Vector2<kt_double> coarseSearchResolution(2 * m_pCorrelationGrid->GetResolution(),
                                              2 * m_pCorrelationGrid->GetResolution());

    // actual scan-matching，进行实质的扫描匹配，返回最佳响应值
    kt_double bestResponse = CorrelateScan(pScan, scanPose, coarseSearchOffset, coarseSearchResolution,
                                           m_pMapper->m_pCoarseSearchAngleOffset->GetValue(),
                                           m_pMapper->m_pCoarseAngleResolution->GetValue(),
                                           doPenalize, rMean, rCovariance, false);

	/* 如果匹配失败，进行更大范围的 scan matching */
    if (m_pMapper->m_pUseResponseExpansion->GetValue() == true)
    {
      if (math::DoubleEqual(bestResponse, 0.0))
      {
#ifdef KARTO_DEBUG
        std::cout << "Mapper Info: Expanding response search space!" << std::endl;
#endif
        // try and increase search angle offset with 20 degrees and do another match
        kt_double newSearchAngleOffset = m_pMapper->m_pCoarseSearchAngleOffset->GetValue();
        for (kt_int32u i = 0; i < 3; i++)
        {
          newSearchAngleOffset += math::DegreesToRadians(20);

          bestResponse = CorrelateScan(pScan, scanPose, coarseSearchOffset, coarseSearchResolution,
                                       newSearchAngleOffset, m_pMapper->m_pCoarseAngleResolution->GetValue(),
                                       doPenalize, rMean, rCovariance, false);

          if (math::DoubleEqual(bestResponse, 0.0) == false)
          {
            break;
          }
        }

#ifdef KARTO_DEBUG
        if (math::DoubleEqual(bestResponse, 0.0))
        {
          std::cout << "Mapper Warning: Unable to calculate response!" << std::endl;
        }
#endif
      }
    }

    /* 做更加精细的匹配 */
	if (doRefineMatch)
    {
      Vector2<kt_double> fineSearchOffset(coarseSearchResolution * 0.5);
      Vector2<kt_double> fineSearchResolution(m_pCorrelationGrid->GetResolution(), m_pCorrelationGrid->GetResolution());
      bestResponse = CorrelateScan(pScan, rMean, fineSearchOffset, fineSearchResolution,
                                   0.5 * m_pMapper->m_pCoarseAngleResolution->GetValue(),
                                   m_pMapper->m_pFineSearchAngleOffset->GetValue(),
                                   doPenalize, rMean, rCovariance, true);
    }


    //std::cout << "BEST POSE = " << rMean << " BEST RESPONSE = " << bestResponse << ",  VARIANCE = " << rCovariance(1, 1) << " " << rCovariance(2, 2) << " " << rCovariance(3, 3) << std::endl;

#ifdef KARTO_DEBUG
    std::cout << "  BEST POSE = " << rMean << " BEST RESPONSE = " << bestResponse << ",  VARIANCE = "
              << rCovariance(0, 0) << ", " << rCovariance(1, 1) << std::endl;
#endif
    assert(math::InRange(rMean.GetHeading(), -KT_PI, KT_PI));

    return bestResponse;
  }

  /**
   * Finds the best pose for the scan centering the search in the correlation grid
   * at the given pose and search in the space by the vector and angular offsets
   * in increments of the given resolutions
   * @param rScan scan to match against correlation grid
   * @param rSearchCenter the center of the search space
   * @param rSearchSpaceOffset searches poses in the area offset by this vector around search center
   * @param rSearchSpaceResolution how fine a granularity to search in the search space
   * @param searchAngleOffset searches poses in the angles offset by this angle around search center
   * @param searchAngleResolution how fine a granularity to search in the angular search space
   * @param doPenalize whether to penalize matches further from the search center
   * @param rMean output parameter of mean (best pose) of match
   * @param rCovariance output parameter of covariance of match
   * @param doingFineMatch whether to do a finer search after coarse search
   * @return strength of response
   */
  /*
  实际的扫描匹配过程
  LocalizedRangeScan* pScan: 实际的扫描线
  const Pose2& rSearchCenter：搜索中心
  rSearchSpaceOffset:搜索空间的偏移量
  rSearchSpaceResolution：搜索空间分辨率
  searchAngleOffset:搜索角度偏移量
  searchAngleResolution：搜索角度分辨率
  doPenalize：是否惩罚远离搜索中心的位置
  rMean：返回最佳位置
  rCovariance:返回匹配的协方差矩阵
  doingFineMatch：是否做精细匹配
  */
  kt_double ScanMatcher::CorrelateScan(LocalizedRangeScan* pScan, const Pose2& rSearchCenter,
                                       const Vector2<kt_double>& rSearchSpaceOffset,
                                       const Vector2<kt_double>& rSearchSpaceResolution,
                                       kt_double searchAngleOffset, kt_double searchAngleResolution,
                                       kt_bool doPenalize, Pose2& rMean, Matrix3& rCovariance, kt_bool doingFineMatch)
  {
    assert(searchAngleResolution != 0.0);

    // setup lookup arrays  建立搜索表格,也就是说建立索引值
    m_pGridLookup->ComputeOffsets(pScan, rSearchCenter.GetHeading(), searchAngleOffset, searchAngleResolution);

    // only initialize probability grid if computing positional covariance (during coarse match)
    if (!doingFineMatch)
    {
      m_pSearchSpaceProbs->Clear();

      // position search grid - finds lower left corner of search grid
      Vector2<kt_double> offset(rSearchCenter.GetPosition() - rSearchSpaceOffset);
      m_pSearchSpaceProbs->GetCoordinateConverter()->SetOffset(offset);  /* 设置偏移值 */
    }

    // calculate position arrays

    std::vector<kt_double> xPoses;
    /* 角度固定后，X 方向需要搜索的次数 */
	kt_int32u nX = static_cast<kt_int32u>(math::Round(rSearchSpaceOffset.GetX() *
                                          2.0 / rSearchSpaceResolution.GetX()) + 1);
    kt_double startX = -rSearchSpaceOffset.GetX();
    for (kt_int32u xIndex = 0; xIndex < nX; xIndex++)
    {
      xPoses.push_back(startX + xIndex * rSearchSpaceResolution.GetX());
    }
    assert(math::DoubleEqual(xPoses.back(), -startX));

	/* 角度固定后，Y 方向需要搜索的次数 */
    std::vector<kt_double> yPoses;
    kt_int32u nY = static_cast<kt_int32u>(math::Round(rSearchSpaceOffset.GetY() *
                                          2.0 / rSearchSpaceResolution.GetY()) + 1);
    kt_double startY = -rSearchSpaceOffset.GetY();
    for (kt_int32u yIndex = 0; yIndex < nY; yIndex++)
    {
      yPoses.push_back(startY + yIndex * rSearchSpaceResolution.GetY());
    }
    assert(math::DoubleEqual(yPoses.back(), -startY));

    // calculate pose response array size
    kt_int32u nAngles = static_cast<kt_int32u>(math::Round(searchAngleOffset * 2.0 / searchAngleResolution) + 1);

	/* 姿势响应的大小 */
    kt_int32u poseResponseSize = static_cast<kt_int32u>(xPoses.size() * yPoses.size() * nAngles);

    // allocate array
    std::pair<kt_double, Pose2>* pPoseResponse = new std::pair<kt_double, Pose2>[poseResponseSize];

	/* 搜索的起始点 */
    Vector2<kt_int32s> startGridPoint = m_pCorrelationGrid->WorldToGrid(Vector2<kt_double>(rSearchCenter.GetX()
                                                                        + startX, rSearchCenter.GetY() + startY));

    kt_int32u poseResponseCounter = 0;
    /* Y 方向的循环 */
	forEachAs(std::vector<kt_double>, &yPoses, yIter)
    {
      kt_double y = *yIter;
      kt_double newPositionY = rSearchCenter.GetY() + y;
      kt_double squareY = math::Square(y); /* 用于计算距离 */
	  
	  /* X 方向的循环 */
      forEachAs(std::vector<kt_double>, &xPoses, xIter)
      {
        kt_double x = *xIter;
        kt_double newPositionX = rSearchCenter.GetX() + x;
        kt_double squareX = math::Square(x); /* 用于计算距离 */

		/* 得到 grid 的索引值 */
        Vector2<kt_int32s> gridPoint = m_pCorrelationGrid->WorldToGrid(Vector2<kt_double>(newPositionX, newPositionY));
        kt_int32s gridIndex = m_pCorrelationGrid->GridIndex(gridPoint);
        assert(gridIndex >= 0);

        kt_double angle = 0.0;
        kt_double startAngle = rSearchCenter.GetHeading() - searchAngleOffset;
		
		/* 角度方向的循环 */
        for (kt_int32u angleIndex = 0; angleIndex < nAngles; angleIndex++)
        {
          angle = startAngle + angleIndex * searchAngleResolution;

		  /* 计算响应值 */
          kt_double response = GetResponse(angleIndex, gridIndex);
		  /* 对偏移做惩罚 */
          if (doPenalize && (math::DoubleEqual(response, 0.0) == false))
          {
            // simple model (approximate Gaussian) to take odometry into account

            kt_double squaredDistance = squareX + squareY;
            kt_double distancePenalty = 1.0 - (DISTANCE_PENALTY_GAIN *
                                               squaredDistance / m_pMapper->m_pDistanceVariancePenalty->GetValue());
            distancePenalty = math::Maximum(distancePenalty, m_pMapper->m_pMinimumDistancePenalty->GetValue());

            kt_double squaredAngleDistance = math::Square(angle - rSearchCenter.GetHeading());
            kt_double anglePenalty = 1.0 - (ANGLE_PENALTY_GAIN *
                                            squaredAngleDistance / m_pMapper->m_pAngleVariancePenalty->GetValue());
            anglePenalty = math::Maximum(anglePenalty, m_pMapper->m_pMinimumAnglePenalty->GetValue());

            response *= (distancePenalty * anglePenalty);
          }

          // store response and pose 保存响应值和对应的位置信息
          pPoseResponse[poseResponseCounter] = std::pair<kt_double, Pose2>(response, Pose2(newPositionX, newPositionY,
                                                                           math::NormalizeAngle(angle)));
          poseResponseCounter++;
        }

        assert(math::DoubleEqual(angle, rSearchCenter.GetHeading() + searchAngleOffset));
      }
    }

    assert(poseResponseSize == poseResponseCounter);

    // find value of best response (in [0; 1]) 
	/* 根据响应值，选择最佳的位置和角度 */
    kt_double bestResponse = -1;
    for (kt_int32u i = 0; i < poseResponseSize; i++)
    {
      bestResponse = math::Maximum(bestResponse, pPoseResponse[i].first);

      // will compute positional covariance, save best relative probability for each cell
	  /* 保存每个网格的最大响应点, 主要是用于计算协方差 */
      if (!doingFineMatch)
      {
        const Pose2& rPose = pPoseResponse[i].second;
        Vector2<kt_int32s> grid = m_pSearchSpaceProbs->WorldToGrid(rPose.GetPosition());

        // Changed (kt_double*) to the reinterpret_cast - Luc
        kt_double* ptr = reinterpret_cast<kt_double*>(m_pSearchSpaceProbs->GetDataPointer(grid));
        if (ptr == NULL)
        {
          throw std::runtime_error("Mapper FATAL ERROR - Index out of range in probability search!");
        }

        *ptr = math::Maximum(pPoseResponse[i].first, *ptr);
      }
    }

    // average all poses with same highest response 如果出现多个相同的最高响应，则进行计算平均值
    Vector2<kt_double> averagePosition;
    kt_double thetaX = 0.0;
    kt_double thetaY = 0.0;
    kt_int32s averagePoseCount = 0;
    for (kt_int32u i = 0; i < poseResponseSize; i++)
    {
      if (math::DoubleEqual2(pPoseResponse[i].first, bestResponse))
      {
        averagePosition += pPoseResponse[i].second.GetPosition();

        kt_double heading = pPoseResponse[i].second.GetHeading();
        thetaX += cos(heading);
        thetaY += sin(heading);

        averagePoseCount++;
      }
    }

    Pose2 averagePose;
    if (averagePoseCount > 0)
    {
      averagePosition /= averagePoseCount;

      thetaX /= averagePoseCount;
      thetaY /= averagePoseCount;

      averagePose = Pose2(averagePosition, atan2(thetaY, thetaX));
    }
    else
    {
      throw std::runtime_error("Mapper FATAL ERROR - Unable to find best position");
    }

    // delete pose response array
    delete [] pPoseResponse;

#ifdef KARTO_DEBUG
    std::cout << "bestPose: " << averagePose << std::endl;
    std::cout << "bestResponse: " << bestResponse << std::endl;
#endif

    if (!doingFineMatch)
    {
      /* 计算位置协方差 */
      ComputePositionalCovariance(averagePose, bestResponse, rSearchCenter, rSearchSpaceOffset,
                                  rSearchSpaceResolution, searchAngleResolution, rCovariance);
    }
    else
    {
	  /* 计算角度协方差 */
      ComputeAngularCovariance(averagePose, bestResponse, rSearchCenter,
                              searchAngleOffset, searchAngleResolution, rCovariance);
    }

    rMean = averagePose;

#ifdef KARTO_DEBUG
    std::cout << "bestPose: " << averagePose << std::endl;
#endif

    if (bestResponse > 1.0)
    {
      bestResponse = 1.0;
    }

    assert(math::InRange(bestResponse, 0.0, 1.0));
    assert(math::InRange(rMean.GetHeading(), -KT_PI, KT_PI));

    return bestResponse;
  }

  /**
   * Computes the positional covariance of the best pose
   * @param rBestPose
   * @param bestResponse
   * @param rSearchCenter
   * @param rSearchSpaceOffset
   * @param rSearchSpaceResolution
   * @param searchAngleResolution
   * @param rCovariance
   */
  void ScanMatcher::ComputePositionalCovariance(const Pose2& rBestPose, kt_double bestResponse,
                                                const Pose2& rSearchCenter,
                                                const Vector2<kt_double>& rSearchSpaceOffset,
                                                const Vector2<kt_double>& rSearchSpaceResolution,
                                                kt_double searchAngleResolution, Matrix3& rCovariance)
  {
    // reset covariance to identity matrix
    rCovariance.SetToIdentity();

    // if best response is vary small return max variance
    if (bestResponse < KT_TOLERANCE)
    {
      rCovariance(0, 0) = MAX_VARIANCE;  // XX
      rCovariance(1, 1) = MAX_VARIANCE;  // YY
      rCovariance(2, 2) = 4 * math::Square(searchAngleResolution);  // TH*TH

      return;
    }

    kt_double accumulatedVarianceXX = 0;
    kt_double accumulatedVarianceXY = 0;
    kt_double accumulatedVarianceYY = 0;
    kt_double norm = 0;

    kt_double dx = rBestPose.GetX() - rSearchCenter.GetX();
    kt_double dy = rBestPose.GetY() - rSearchCenter.GetY();

    kt_double offsetX = rSearchSpaceOffset.GetX();
    kt_double offsetY = rSearchSpaceOffset.GetY();

    kt_int32u nX = static_cast<kt_int32u>(math::Round(offsetX * 2.0 / rSearchSpaceResolution.GetX()) + 1);
    kt_double startX = -offsetX;
    assert(math::DoubleEqual(startX + (nX - 1) * rSearchSpaceResolution.GetX(), -startX));

    kt_int32u nY = static_cast<kt_int32u>(math::Round(offsetY * 2.0 / rSearchSpaceResolution.GetY()) + 1);
    kt_double startY = -offsetY;
    assert(math::DoubleEqual(startY + (nY - 1) * rSearchSpaceResolution.GetY(), -startY));

    for (kt_int32u yIndex = 0; yIndex < nY; yIndex++)
    {
      kt_double y = startY + yIndex * rSearchSpaceResolution.GetY();

      for (kt_int32u xIndex = 0; xIndex < nX; xIndex++)
      {
        kt_double x = startX + xIndex * rSearchSpaceResolution.GetX();

        Vector2<kt_int32s> gridPoint = m_pSearchSpaceProbs->WorldToGrid(Vector2<kt_double>(rSearchCenter.GetX() + x,
                                                                                           rSearchCenter.GetY() + y));
        kt_double response = *(m_pSearchSpaceProbs->GetDataPointer(gridPoint));

        // response is not a low response
        if (response >= (bestResponse - 0.1))
        {
          norm += response;
          accumulatedVarianceXX += (math::Square(x - dx) * response);
          accumulatedVarianceXY += ((x - dx) * (y - dy) * response);
          accumulatedVarianceYY += (math::Square(y - dy) * response);
        }
      }
    }

    if (norm > KT_TOLERANCE)
    {
      kt_double varianceXX = accumulatedVarianceXX / norm;
      kt_double varianceXY = accumulatedVarianceXY / norm;
      kt_double varianceYY = accumulatedVarianceYY / norm;
      kt_double varianceTHTH = 4 * math::Square(searchAngleResolution);

      // lower-bound variances so that they are not too small;
      // ensures that links are not too tight
      kt_double minVarianceXX = 0.1 * math::Square(rSearchSpaceResolution.GetX());
      kt_double minVarianceYY = 0.1 * math::Square(rSearchSpaceResolution.GetY());
      varianceXX = math::Maximum(varianceXX, minVarianceXX);
      varianceYY = math::Maximum(varianceYY, minVarianceYY);

      // increase variance for poorer responses
      kt_double multiplier = 1.0 / bestResponse;
      rCovariance(0, 0) = varianceXX * multiplier;
      rCovariance(0, 1) = varianceXY * multiplier;
      rCovariance(1, 0) = varianceXY * multiplier;
      rCovariance(1, 1) = varianceYY * multiplier;
      rCovariance(2, 2) = varianceTHTH;  // this value will be set in ComputeAngularCovariance
    }

    // if values are 0, set to MAX_VARIANCE
    // values might be 0 if points are too sparse and thus don't hit other points
    if (math::DoubleEqual(rCovariance(0, 0), 0.0))
    {
      rCovariance(0, 0) = MAX_VARIANCE;
    }

    if (math::DoubleEqual(rCovariance(1, 1), 0.0))
    {
      rCovariance(1, 1) = MAX_VARIANCE;
    }
  }

  /**
   * Computes the angular covariance of the best pose
   * @param rBestPose
   * @param bestResponse
   * @param rSearchCenter
   * @param rSearchAngleOffset
   * @param searchAngleResolution
   * @param rCovariance
   */
  void ScanMatcher::ComputeAngularCovariance(const Pose2& rBestPose,
                                             kt_double bestResponse,
                                             const Pose2& rSearchCenter,
                                             kt_double searchAngleOffset,
                                             kt_double searchAngleResolution,
                                             Matrix3& rCovariance)
  {
    // NOTE: do not reset covariance matrix

    // normalize angle difference
    kt_double bestAngle = math::NormalizeAngleDifference(rBestPose.GetHeading(), rSearchCenter.GetHeading());

    Vector2<kt_int32s> gridPoint = m_pCorrelationGrid->WorldToGrid(rBestPose.GetPosition());
    kt_int32s gridIndex = m_pCorrelationGrid->GridIndex(gridPoint);

    kt_int32u nAngles = static_cast<kt_int32u>(math::Round(searchAngleOffset * 2 / searchAngleResolution) + 1);

    kt_double angle = 0.0;
    kt_double startAngle = rSearchCenter.GetHeading() - searchAngleOffset;

    kt_double norm = 0.0;
    kt_double accumulatedVarianceThTh = 0.0;
    for (kt_int32u angleIndex = 0; angleIndex < nAngles; angleIndex++)
    {
      angle = startAngle + angleIndex * searchAngleResolution;
      kt_double response = GetResponse(angleIndex, gridIndex);

      // response is not a low response
      if (response >= (bestResponse - 0.1))
      {
        norm += response;
        accumulatedVarianceThTh += (math::Square(angle - bestAngle) * response);
      }
    }
    assert(math::DoubleEqual(angle, rSearchCenter.GetHeading() + searchAngleOffset));

    if (norm > KT_TOLERANCE)
    {
      if (accumulatedVarianceThTh < KT_TOLERANCE)
      {
        accumulatedVarianceThTh = math::Square(searchAngleResolution);
      }

      accumulatedVarianceThTh /= norm;
    }
    else
    {
      accumulatedVarianceThTh = 1000 * math::Square(searchAngleResolution);
    }

    rCovariance(2, 2) = accumulatedVarianceThTh;
  }

  /**
   * Marks cells where scans' points hit as being occupied
   * @param rScans scans whose points will mark cells in grid as being occupied         以前所有的扫描线
   * @param viewPoint do not add points that belong to scans "opposite" the view point  viewPoint 扫描器的位置信息
   */
  void ScanMatcher::AddScans(const LocalizedRangeScanVector& rScans, Vector2<kt_double> viewPoint)
  {
    m_pCorrelationGrid->Clear(); /* 清除历史数据 */

    // add all scans to grid
    const_forEach(LocalizedRangeScanVector, &rScans)
    {
      AddScan(*iter, viewPoint);
    }
  }

  /**
   * Marks cells where scans' points hit as being occupied.  Can smear points as they are added.
   * @param pScan scan whose points will mark cells in grid as being occupied  扫描线
   * @param viewPoint do not add points that belong to scans "opposite" the view point 不增加与rViewPoint反向的点信息
   * @param doSmear whether the points will be smeared 是否做高斯平滑操作?
   */
  void ScanMatcher::AddScan(LocalizedRangeScan* pScan, const Vector2<kt_double>& rViewPoint, kt_bool doSmear)
  {
    PointVectorDouble validPoints = FindValidPoints(pScan, rViewPoint);

    // put in all valid points
    const_forEach(PointVectorDouble, &validPoints)
    {
      // 转换成网格坐标
	  Vector2<kt_int32s> gridPoint = m_pCorrelationGrid->WorldToGrid(*iter);
      
	  /* 超出网格的范围，直接忽略 */
	  if (!math::IsUpTo(gridPoint.GetX(), m_pCorrelationGrid->GetROI().GetWidth()) ||
          !math::IsUpTo(gridPoint.GetY(), m_pCorrelationGrid->GetROI().GetHeight()))
      {
        // point not in grid
        continue;
      }

      int gridIndex = m_pCorrelationGrid->GridIndex(gridPoint);

      // set grid cell as occupied  设置对应的 cell 为占用了
      if (m_pCorrelationGrid->GetDataPointer()[gridIndex] == GridStates_Occupied)
      {
        // 先前已经设置
        // value already set 
        continue;
      }

      m_pCorrelationGrid->GetDataPointer()[gridIndex] = GridStates_Occupied;

      // smear grid
      if (doSmear == true)
      {
        m_pCorrelationGrid->SmearPoint(gridPoint); /* 相当于做滤波操作 */
      }
    }
  }

  /**
   * Compute which points in a scan are on the same side as the given viewpoint 判断哪些点与 viewpoint 处于相同的边
   * @param pScan
   * @param rViewPoint
   * @return points on the same side
   */
  /* 原理不是很清楚，计算得到与视点处于一遍的所有扫描点？？？？？？ */
  PointVectorDouble ScanMatcher::FindValidPoints(LocalizedRangeScan* pScan, const Vector2<kt_double>& rViewPoint) const
  {
    const PointVectorDouble& rPointReadings = pScan->GetPointReadings();

    // points must be at least 10 cm away when making comparisons of inside/outside of viewpoint
    const kt_double minSquareDistance = math::Square(0.1);  // in m^2

    // this iterator lags from the main iterator adding points only when the points are on
    // the same side as the viewpoint
    PointVectorDouble::const_iterator trailingPointIter = rPointReadings.begin();
    PointVectorDouble validPoints;

    Vector2<kt_double> firstPoint;
    kt_bool firstTime = true;
    const_forEach(PointVectorDouble, &rPointReadings)
    {
      Vector2<kt_double> currentPoint = *iter;

      if (firstTime && !isnan(currentPoint.GetX()) && !isnan(currentPoint.GetY()))
      {
        firstPoint = currentPoint;
        firstTime = false;
      }

      Vector2<kt_double> delta = firstPoint - currentPoint;
      if (delta.SquaredLength() > minSquareDistance)
      {
        // This compute the Determinant (viewPoint FirstPoint, viewPoint currentPoint)
        // Which computes the direction of rotation, if the rotation is counterclock
        // wise then we are looking at data we should keep. If it's negative rotation
        // we should not included in in the matching
        // have enough distance, check viewpoint
        /* 先用两点确定一条线，然后进行判断 */
        double a = rViewPoint.GetY() - firstPoint.GetY();
        double b = firstPoint.GetX() - rViewPoint.GetX();
        double c = firstPoint.GetY() * rViewPoint.GetX() - firstPoint.GetX() * rViewPoint.GetY();
        double ss = currentPoint.GetX() * a + currentPoint.GetY() * b + c;

        // reset beginning point
        firstPoint = currentPoint;

		// 2017/02/15 shenyanfei 
        if (ss < 0.0)  // wrong side, skip and keep going
		// if (ss > 0.0)
		{
          trailingPointIter = iter;
        }
        else
        {
          for (; trailingPointIter != iter; ++trailingPointIter)
          {
            validPoints.push_back(*trailingPointIter);
          }
        }
      }
    }

    return validPoints;
  }

  /**
   * Get response at given position for given rotation (only look up valid points)
   * @param angleIndex
   * @param gridPositionIndex
   * @return response
   根据给定的角度和grid索引值计算响应,明白其中的物理原理,特别是其中的双重位置偏移
   */
  kt_double ScanMatcher::GetResponse(kt_int32u angleIndex, kt_int32s gridPositionIndex) const
  {
    kt_double response = 0.0;

    // add up value for each point
    kt_int8u* pByte = m_pCorrelationGrid->GetDataPointer() + gridPositionIndex;

    const LookupArray* pOffsets = m_pGridLookup->GetLookupArray(angleIndex);
    assert(pOffsets != NULL);

    // get number of points in offset list
    kt_int32u nPoints = pOffsets->GetSize();
    if (nPoints == 0)
    {
      return response;
    }

    // calculate response
    kt_int32s* pAngleIndexPointer = pOffsets->GetArrayPointer();
    for (kt_int32u i = 0; i < nPoints; i++)
    {
      // ignore points that fall off the grid
      kt_int32s pointGridIndex = gridPositionIndex + pAngleIndexPointer[i]; /* 计算偏移量 */
	  /* 超出表格的范围或者是无效的扫描点 */
      if (!math::IsUpTo(pointGridIndex, m_pCorrelationGrid->GetDataSize()) || pAngleIndexPointer[i] == INVALID_SCAN)
      {
        continue;
      }

      // uses index offsets to efficiently find location of point in the grid
	  /* 这个是实际计算响应的地方 */
      response += pByte[pAngleIndexPointer[i]];
    }

    // normalize response
    response /= (nPoints * GridStates_Occupied); /* 相当于部分占用点与全部占用电的比例 */
    assert(fabs(response) <= 1.0);

    return response; /* 返回响应值 */
  }


  ////////////////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////////////

  template<typename T>
  class BreadthFirstTraversal : public GraphTraversal<T>
  {
  public:
    /**
     * Constructs a breadth-first traverser for the given graph
     */
    BreadthFirstTraversal(Graph<T>* pGraph)
    : GraphTraversal<T>(pGraph)
    {
    }

    /**
     * Destructor
     */
    virtual ~BreadthFirstTraversal()
    {
    }

  public:
    /**
     * Traverse the graph starting with the given vertex; applies the visitor to visited nodes
     * @param pStartVertex
     * @param pVisitor
     * @return visited vertices
     */
    virtual std::vector<T*> Traverse(Vertex<T>* pStartVertex, Visitor<T>* pVisitor)
    {
      std::queue<Vertex<T>*> toVisit;
      std::set<Vertex<T>*> seenVertices;
      std::vector<Vertex<T>*> validVertices;

      toVisit.push(pStartVertex);
      seenVertices.insert(pStartVertex);

      // 不停地搜索队列toVisit，将与队头节点相邻的且与pVisitor位置距离小于4的vertex加入队尾，并推入validVertices
      // 最终validVertices即为以当前vertex为起始，倒退回去的vertex数组
      do
      {
        Vertex<T>* pNext = toVisit.front();
        toVisit.pop();

        // pVisitor的位置与参数pNext的重心位置距离小于m_MaxDistanceSquared - KT_TOLERANCE时返回true，否则false
        if (pVisitor->Visit(pNext))
        {
          // vertex is valid, explore neighbors
          validVertices.push_back(pNext);

          // 获取相邻vertex
          std::vector<Vertex<T>*> adjacentVertices = pNext->GetAdjacentVertices();
		  forEachMe(typename std::vector<Vertex<T>*>, &adjacentVertices)
          {
            Vertex<T>* pAdjacent = *iter;

            // adjacent vertex has not yet been seen, add to queue for processing
            if (seenVertices.find(pAdjacent) == seenVertices.end())
            {
              toVisit.push(pAdjacent);
              seenVertices.insert(pAdjacent);
            }
          }
        }
      } while (toVisit.empty() == false);

      // 得到与validVertices对应的LocalizedRangeScan
      std::vector<T*> objects;
	  forEachMe(typename std::vector<Vertex<T>*>, &validVertices)
      {
        objects.push_back((*iter)->GetObject());
      }

      return objects;
    }
  };  // class BreadthFirstTraversal

  ////////////////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////////////

  class NearScanVisitor : public Visitor<LocalizedRangeScan>
  {
  public:
    NearScanVisitor(LocalizedRangeScan* pScan, kt_double maxDistance, kt_bool useScanBarycenter)
      : m_MaxDistanceSquared(math::Square(maxDistance))
      , m_UseScanBarycenter(useScanBarycenter)
    {
      m_CenterPose = pScan->GetReferencePose(m_UseScanBarycenter);
    }

    // 该Visitor的位置与参数pVertex的重心位置距离小于m_MaxDistanceSquared - KT_TOLERANCE时返回true，否则false
    virtual kt_bool Visit(Vertex<LocalizedRangeScan>* pVertex)
    {
      LocalizedRangeScan* pScan = pVertex->GetObject();

      Pose2 pose = pScan->GetReferencePose(m_UseScanBarycenter);

      kt_double squaredDistance = pose.GetPosition().SquaredDistance(m_CenterPose.GetPosition());
      return (squaredDistance <= m_MaxDistanceSquared - KT_TOLERANCE);
    }

  protected:
    Pose2 m_CenterPose;
    kt_double m_MaxDistanceSquared;
    kt_bool m_UseScanBarycenter;
  };  // NearScanVisitor

  ////////////////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////////////

  MapperGraph::MapperGraph(Mapper* pMapper, kt_double rangeThreshold)
    : m_pMapper(pMapper)
  {
    m_pLoopScanMatcher = ScanMatcher::Create(pMapper, m_pMapper->m_pLoopSearchSpaceDimension->GetValue(),
                                             m_pMapper->m_pLoopSearchSpaceResolution->GetValue(),
                                             m_pMapper->m_pLoopSearchSpaceSmearDeviation->GetValue(), rangeThreshold);
    assert(m_pLoopScanMatcher);

    m_pTraversal = new BreadthFirstTraversal<LocalizedRangeScan>(this);
  }

  MapperGraph::~MapperGraph()
  {
    delete m_pLoopScanMatcher;
    m_pLoopScanMatcher = NULL;

    delete m_pTraversal;
    m_pTraversal = NULL;
  }

  void MapperGraph::AddVertex(LocalizedRangeScan* pScan)
  {
    assert(pScan);

    if (pScan != NULL)
    {
      Vertex<LocalizedRangeScan>* pVertex = new Vertex<LocalizedRangeScan>(pScan);
      Graph<LocalizedRangeScan>::AddVertex(pScan->GetSensorName(), pVertex);
      if (m_pMapper->m_pScanOptimizer != NULL)
      {
        m_pMapper->m_pScanOptimizer->AddNode(pVertex);
      }
    }
  }

  void MapperGraph::AddEdges(LocalizedRangeScan* pScan, const Matrix3& rCovariance)
  {
    MapperSensorManager* pSensorManager = m_pMapper->m_pMapperSensorManager;

    const Name& rSensorName = pScan->GetSensorName();

    // link to previous scan
    kt_int32s previousScanNum = pScan->GetStateId() - 1;
    if (pSensorManager->GetLastScan(rSensorName) != NULL)
    {
      assert(previousScanNum >= 0);
      LinkScans(pSensorManager->GetScan(rSensorName, previousScanNum), pScan, pScan->GetSensorPose(), rCovariance);
    }

    Pose2Vector means;
    std::vector<Matrix3> covariances;

    // first scan (link to first scan of other robots)
    if (pSensorManager->GetLastScan(rSensorName) == NULL)
    {
      assert(pSensorManager->GetScans(rSensorName).size() == 1);

      std::vector<Name> deviceNames = pSensorManager->GetSensorNames();
	  forEachMe(std::vector<Name>, &deviceNames)
      {
        const Name& rCandidateSensorName = *iter;

        // skip if candidate device is the same or other device has no scans
        if ((rCandidateSensorName == rSensorName) || (pSensorManager->GetScans(rCandidateSensorName).empty()))
        {
          continue;
        }

        Pose2 bestPose;
        Matrix3 covariance;
        kt_double response = m_pMapper->m_pSequentialScanMatcher->MatchScan(pScan,
                                                                  pSensorManager->GetScans(rCandidateSensorName),
                                                                  bestPose, covariance);
        LinkScans(pScan, pSensorManager->GetScan(rCandidateSensorName, 0), bestPose, covariance);

        // only add to means and covariances if response was high "enough"
        if (response > m_pMapper->m_pLinkMatchMinimumResponseFine->GetValue())
        {
          means.push_back(bestPose);
          covariances.push_back(covariance);
        }
      }
    }
    else
    {
      // link to running scans
      Pose2 scanPose = pScan->GetSensorPose();
      means.push_back(scanPose);
      covariances.push_back(rCovariance);
      LinkChainToScan(pSensorManager->GetRunningScans(rSensorName), pScan, scanPose, rCovariance);
    }

    // link to other near chains (chains that include new scan are invalid)
    LinkNearChains(pScan, means, covariances);

    if (!means.empty())
    {
      pScan->SetSensorPose(ComputeWeightedMean(means, covariances));
    }
  }

  kt_bool MapperGraph::TryCloseLoop(LocalizedRangeScan* pScan, const Name& rSensorName)
  {
    kt_bool loopClosed = false;

    kt_int32u scanIndex = 0;

    // 从当前scanIndex开始寻找可能是回环的候选链
    // 候选链由那些与当前Scan位置相近，时间间隔较远的连续Scan组成
    LocalizedRangeScanVector candidateChain = FindPossibleLoopClosure(pScan, rSensorName, scanIndex);

    // 继续从当前scanIndex开始，寻找候选链，直到找完所有scan
    while (!candidateChain.empty())
    {
      Pose2 bestPose;
      Matrix3 covariance;
      // 当前Scan与candidateChain所在区域进行match
      kt_double coarseResponse = m_pLoopScanMatcher->MatchScan(pScan, candidateChain,
                                                               bestPose, covariance, false, false);

      std::stringstream stream;
      stream << "COARSE RESPONSE: " << coarseResponse
             << " (> " << m_pMapper->m_pLoopMatchMinimumResponseCoarse->GetValue() << ")"
             << std::endl;
      stream << "            var: " << covariance(0, 0) << ",  " << covariance(1, 1)
             << " (< " << m_pMapper->m_pLoopMatchMaximumVarianceCoarse->GetValue() << ")";

      std::cout << stream.str() << std::endl;
      m_pMapper->FireLoopClosureCheck(stream.str());

      if ((coarseResponse > m_pMapper->m_pLoopMatchMinimumResponseCoarse->GetValue()) &&
          (covariance(0, 0) < m_pMapper->m_pLoopMatchMaximumVarianceCoarse->GetValue()) &&
          (covariance(1, 1) < m_pMapper->m_pLoopMatchMaximumVarianceCoarse->GetValue()))
      {
        LocalizedRangeScan tmpScan(pScan->GetSensorName(), pScan->GetRangeReadingsVector());
        tmpScan.SetUniqueId(pScan->GetUniqueId());
        tmpScan.SetTime(pScan->GetTime());
        tmpScan.SetStateId(pScan->GetStateId());
        tmpScan.SetCorrectedPose(pScan->GetCorrectedPose());
        tmpScan.SetSensorPose(bestPose);  // This also updates OdometricPose.
        kt_double fineResponse = m_pMapper->m_pSequentialScanMatcher->MatchScan(&tmpScan, candidateChain,
                                                                                bestPose, covariance, false);

        std::stringstream stream1;
        stream1 << "FINE RESPONSE: " << fineResponse << " (>"
                << m_pMapper->m_pLoopMatchMinimumResponseFine->GetValue() << ")" << std::endl;
        m_pMapper->FireLoopClosureCheck(stream1.str());

        if (fineResponse < m_pMapper->m_pLoopMatchMinimumResponseFine->GetValue())
        {
          m_pMapper->FireLoopClosureCheck("REJECTED!");
        }
        else
        {
          m_pMapper->FireBeginLoopClosure("Closing loop...");

          pScan->SetSensorPose(bestPose);
          // 将pScan与candidateChain中与其最近的Scan相连
          LinkChainToScan(candidateChain, pScan, bestPose, covariance);
          CorrectPoses();// 在这里进行了地图优化

          m_pMapper->FireEndLoopClosure("Loop closed!");

          loopClosed = true;
        }
      }

      candidateChain = FindPossibleLoopClosure(pScan, rSensorName, scanIndex);
    }

    return loopClosed;
  }

  LocalizedRangeScan* MapperGraph::GetClosestScanToPose(const LocalizedRangeScanVector& rScans,
                                                        const Pose2& rPose) const
  {
    LocalizedRangeScan* pClosestScan = NULL;
    kt_double bestSquaredDistance = DBL_MAX;

    const_forEach(LocalizedRangeScanVector, &rScans)
    {
      Pose2 scanPose = (*iter)->GetReferencePose(m_pMapper->m_pUseScanBarycenter->GetValue());

      kt_double squaredDistance = rPose.GetPosition().SquaredDistance(scanPose.GetPosition());
      if (squaredDistance < bestSquaredDistance)
      {
        bestSquaredDistance = squaredDistance;
        pClosestScan = *iter;
      }
    }

    return pClosestScan;
  }

  Edge<LocalizedRangeScan>* MapperGraph::AddEdge(LocalizedRangeScan* pSourceScan,
                                                 LocalizedRangeScan* pTargetScan, kt_bool& rIsNewEdge)
  {
    // check that vertex exists
    assert(pSourceScan->GetStateId() < (kt_int32s)m_Vertices[pSourceScan->GetSensorName()].size());
    assert(pTargetScan->GetStateId() < (kt_int32s)m_Vertices[pTargetScan->GetSensorName()].size());

    Vertex<LocalizedRangeScan>* v1 = m_Vertices[pSourceScan->GetSensorName()][pSourceScan->GetStateId()];
    Vertex<LocalizedRangeScan>* v2 = m_Vertices[pTargetScan->GetSensorName()][pTargetScan->GetStateId()];

    // see if edge already exists
    const_forEach(std::vector<Edge<LocalizedRangeScan>*>, &(v1->GetEdges()))
    {
      Edge<LocalizedRangeScan>* pEdge = *iter;

      if (pEdge->GetTarget() == v2)
      {
        rIsNewEdge = false;
        return pEdge;
      }
    }

    Edge<LocalizedRangeScan>* pEdge = new Edge<LocalizedRangeScan>(v1, v2);
    Graph<LocalizedRangeScan>::AddEdge(pEdge);
    rIsNewEdge = true;
    return pEdge;
  }

  void MapperGraph::LinkScans(LocalizedRangeScan* pFromScan, LocalizedRangeScan* pToScan,
                              const Pose2& rMean, const Matrix3& rCovariance)
  {
    kt_bool isNewEdge = true;
    Edge<LocalizedRangeScan>* pEdge = AddEdge(pFromScan, pToScan, isNewEdge);

    // only attach link information if the edge is new
    if (isNewEdge == true)
    {
      pEdge->SetLabel(new LinkInfo(pFromScan->GetSensorPose(), rMean, rCovariance));
      if (m_pMapper->m_pScanOptimizer != NULL)
      {
        m_pMapper->m_pScanOptimizer->AddConstraint(pEdge);
      }
    }
  }

  void MapperGraph::LinkNearChains(LocalizedRangeScan* pScan, Pose2Vector& rMeans, std::vector<Matrix3>& rCovariances)
  {
    const std::vector<LocalizedRangeScanVector> nearChains = FindNearChains(pScan);
    const_forEach(std::vector<LocalizedRangeScanVector>, &nearChains)
    {
      if (iter->size() < m_pMapper->m_pLoopMatchMinimumChainSize->GetValue())
      {
        continue;
      }

      Pose2 mean;
      Matrix3 covariance;
      // match scan against "near" chain
      kt_double response = m_pMapper->m_pSequentialScanMatcher->MatchScan(pScan, *iter, mean, covariance, false);
      if (response > m_pMapper->m_pLinkMatchMinimumResponseFine->GetValue() - KT_TOLERANCE)
      {
        rMeans.push_back(mean);
        rCovariances.push_back(covariance);
        LinkChainToScan(*iter, pScan, mean, covariance);
      }
    }
  }

// 将pScan与rChain中与其最近的Scan相连（质心最接近的）
  void MapperGraph::LinkChainToScan(const LocalizedRangeScanVector& rChain, LocalizedRangeScan* pScan,
                                    const Pose2& rMean, const Matrix3& rCovariance)
  {
    Pose2 pose = pScan->GetReferencePose(m_pMapper->m_pUseScanBarycenter->GetValue());

    LocalizedRangeScan* pClosestScan = GetClosestScanToPose(rChain, pose);
    assert(pClosestScan != NULL);

    Pose2 closestScanPose = pClosestScan->GetReferencePose(m_pMapper->m_pUseScanBarycenter->GetValue());

    kt_double squaredDistance = pose.GetPosition().SquaredDistance(closestScanPose.GetPosition());
    if (squaredDistance < math::Square(m_pMapper->m_pLinkScanMaximumDistance->GetValue()) + KT_TOLERANCE)
    {
      LinkScans(pClosestScan, pScan, rMean, rCovariance);
    }
  }

  std::vector<LocalizedRangeScanVector> MapperGraph::FindNearChains(LocalizedRangeScan* pScan)
  {
    std::vector<LocalizedRangeScanVector> nearChains;

    Pose2 scanPose = pScan->GetReferencePose(m_pMapper->m_pUseScanBarycenter->GetValue());

    // to keep track of which scans have been added to a chain
    LocalizedRangeScanVector processed;

    const LocalizedRangeScanVector nearLinkedScans = FindNearLinkedScans(pScan,
                                                     m_pMapper->m_pLinkScanMaximumDistance->GetValue());
    const_forEach(LocalizedRangeScanVector, &nearLinkedScans)
    {
      LocalizedRangeScan* pNearScan = *iter;

      if (pNearScan == pScan)
      {
        continue;
      }

      // scan has already been processed, skip
      if (find(processed.begin(), processed.end(), pNearScan) != processed.end())
      {
        continue;
      }

      processed.push_back(pNearScan);

      // build up chain
      kt_bool isValidChain = true;
      std::list<LocalizedRangeScan*> chain;

      // add scans before current scan being processed
      for (kt_int32s candidateScanNum = pNearScan->GetStateId() - 1; candidateScanNum >= 0; candidateScanNum--)
      {
        LocalizedRangeScan* pCandidateScan = m_pMapper->m_pMapperSensorManager->GetScan(pNearScan->GetSensorName(),
                                                                                        candidateScanNum);

        // chain is invalid--contains scan being added
        if (pCandidateScan == pScan)
        {
          isValidChain = false;
        }

        Pose2 candidatePose = pCandidateScan->GetReferencePose(m_pMapper->m_pUseScanBarycenter->GetValue());
        kt_double squaredDistance = scanPose.GetPosition().SquaredDistance(candidatePose.GetPosition());

        if (squaredDistance < math::Square(m_pMapper->m_pLinkScanMaximumDistance->GetValue()) + KT_TOLERANCE)
        {
          chain.push_front(pCandidateScan);
          processed.push_back(pCandidateScan);
        }
        else
        {
          break;
        }
      }

      chain.push_back(pNearScan);

      // add scans after current scan being processed
      kt_int32u end = static_cast<kt_int32u>(m_pMapper->m_pMapperSensorManager->GetScans(pNearScan->GetSensorName()).size());
      for (kt_int32u candidateScanNum = pNearScan->GetStateId() + 1; candidateScanNum < end; candidateScanNum++)
      {
        LocalizedRangeScan* pCandidateScan = m_pMapper->m_pMapperSensorManager->GetScan(pNearScan->GetSensorName(),
                                                                                        candidateScanNum);

        if (pCandidateScan == pScan)
        {
          isValidChain = false;
        }

        Pose2 candidatePose = pCandidateScan->GetReferencePose(m_pMapper->m_pUseScanBarycenter->GetValue());;
        kt_double squaredDistance = scanPose.GetPosition().SquaredDistance(candidatePose.GetPosition());

        if (squaredDistance < math::Square(m_pMapper->m_pLinkScanMaximumDistance->GetValue()) + KT_TOLERANCE)
        {
          chain.push_back(pCandidateScan);
          processed.push_back(pCandidateScan);
        }
        else
        {
          break;
        }
      }

      if (isValidChain)
      {
        // change list to vector
        LocalizedRangeScanVector tempChain;
        std::copy(chain.begin(), chain.end(), std::inserter(tempChain, tempChain.begin()));
        // add chain to collection
        nearChains.push_back(tempChain);
      }
    }

    return nearChains;
  }

  LocalizedRangeScanVector MapperGraph::FindNearLinkedScans(LocalizedRangeScan* pScan, kt_double maxDistance)
  {
    NearScanVisitor* pVisitor = new NearScanVisitor(pScan, maxDistance, m_pMapper->m_pUseScanBarycenter->GetValue());
    LocalizedRangeScanVector nearLinkedScans = m_pTraversal->Traverse(GetVertex(pScan), pVisitor);// Traverse在类BreadthFirstTraversal里, 1117
    delete pVisitor;

    return nearLinkedScans;
  }

  Pose2 MapperGraph::ComputeWeightedMean(const Pose2Vector& rMeans, const std::vector<Matrix3>& rCovariances) const
  {
    assert(rMeans.size() == rCovariances.size());

    // compute sum of inverses and create inverse list
    std::vector<Matrix3> inverses;
    inverses.reserve(rCovariances.size());

    Matrix3 sumOfInverses;
    const_forEach(std::vector<Matrix3>, &rCovariances)
    {
      Matrix3 inverse = iter->Inverse();
      inverses.push_back(inverse);

      sumOfInverses += inverse;
    }
    Matrix3 inverseOfSumOfInverses = sumOfInverses.Inverse();

    // compute weighted mean
    Pose2 accumulatedPose;
    kt_double thetaX = 0.0;
    kt_double thetaY = 0.0;

    Pose2Vector::const_iterator meansIter = rMeans.begin();
    const_forEach(std::vector<Matrix3>, &inverses)
    {
      Pose2 pose = *meansIter;
      kt_double angle = pose.GetHeading();
      thetaX += cos(angle);
      thetaY += sin(angle);

      Matrix3 weight = inverseOfSumOfInverses * (*iter);
      accumulatedPose += weight * pose;

      ++meansIter;
    }

    thetaX /= rMeans.size();
    thetaY /= rMeans.size();
    accumulatedPose.SetHeading(atan2(thetaY, thetaX));

    return accumulatedPose;
  }

  LocalizedRangeScanVector MapperGraph::FindPossibleLoopClosure(LocalizedRangeScan* pScan,
                                                                const Name& rSensorName,
                                                                kt_int32u& rStartNum)
  {
    LocalizedRangeScanVector chain;  // return value

    // 参数为true则返回扫描点的重心，否则返回sensor的位置（基于世界坐标系）。此为true
    Pose2 pose = pScan->GetReferencePose(m_pMapper->m_pUseScanBarycenter->GetValue());

    // possible loop closure chain should not include close scans that have a
    // path of links to the scan of interest
    // 此m_pLoopSearchMaximumDistance默认为4，找到小于该距离的附近先前的连续的Scans
    const LocalizedRangeScanVector nearLinkedScans =
          FindNearLinkedScans(pScan, m_pMapper->m_pLoopSearchMaximumDistance->GetValue());

    // 获取Scans的个数
    kt_int32u nScans = static_cast<kt_int32u>(m_pMapper->m_pMapperSensorManager->GetScans(rSensorName).size());
    // nScans越大，循环所花时间越长
    for (; rStartNum < nScans; rStartNum++)
    {
      //获取第rStartNum个Scan
      LocalizedRangeScan* pCandidateScan = m_pMapper->m_pMapperSensorManager->GetScan(rSensorName, rStartNum);

      //获取第rStartNum个Scan的位置（基于世界坐标系）
      Pose2 candidateScanPose = pCandidateScan->GetReferencePose(m_pMapper->m_pUseScanBarycenter->GetValue());

      //得到当前Scan与候选Scan之间距离的平方。如果该距离足够小( < 4 )，并且候选Scan不在nearLinkedScans里，
	  //则将候选Scan加入chain里。当有足够多的连续满足上述条件的Scan时（注意是连续的），则返回chain，
	  //否则只要不连续就清空chain。
      kt_double squaredDistance = candidateScanPose.GetPosition().SquaredDistance(pose.GetPosition());
      if (squaredDistance < math::Square(m_pMapper->m_pLoopSearchMaximumDistance->GetValue()) + KT_TOLERANCE)
      {
        // a linked scan cannot be in the chain
        if (find(nearLinkedScans.begin(), nearLinkedScans.end(), pCandidateScan) != nearLinkedScans.end())
        {
          chain.clear();
        }
        else
        {
          chain.push_back(pCandidateScan);
        }
      }
      else
      {
        // return chain if it is long "enough"
        if (chain.size() >= m_pMapper->m_pLoopMatchMinimumChainSize->GetValue())
        {
          return chain;
        }
        else
        {
          chain.clear();
        }
      }
    }

    // added by myself，除去过短的chain
    if(chain.size() < m_pMapper->m_pLoopMatchMinimumChainSize->GetValue())
        chain.clear();

    return chain;
  }

  void MapperGraph::CorrectPoses()
  {
    // optimize scans!
    ScanSolver* pSolver = m_pMapper->m_pScanOptimizer;
    if (pSolver != NULL)
    {
      pSolver->Compute();

      const_forEach(ScanSolver::IdPoseVector, &pSolver->GetCorrections())
      {
        m_pMapper->m_pMapperSensorManager->GetScan(iter->first)->SetSensorPose(iter->second);
      }

      pSolver->Clear();
    }
  }

  ////////////////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////////////////////////////////////

  /**
   * Default constructor
   */
  Mapper::Mapper()
    : Module("Mapper")
    , m_Initialized(false)
    , m_pSequentialScanMatcher(NULL)
    , m_pMapperSensorManager(NULL)
    , m_pGraph(NULL)
    , m_pScanOptimizer(NULL)
  {
    InitializeParameters();
  }

  /**
   * Default constructor
   */
  Mapper::Mapper(const std::string& rName)
    : Module(rName)
    , m_Initialized(false)
    , m_pSequentialScanMatcher(NULL)
    , m_pMapperSensorManager(NULL)
    , m_pGraph(NULL)
    , m_pScanOptimizer(NULL)
  {
    InitializeParameters();
  }

  /**
   * Destructor
   */
  Mapper::~Mapper()
  {
    Reset();

    delete m_pMapperSensorManager;
  }

  // 初始化一些缺省参数
  void Mapper::InitializeParameters()
  {
    /* 是否使用 scan matching */
    m_pUseScanMatching = new Parameter<kt_bool>(
        "UseScanMatching",
        "When set to true, the mapper will use a scan matching algorithm. "
        "In most real-world situations this should be set to true so that the "
        "mapper algorithm can correct for noise and errors in odometry and "
        "scan data. In some simulator environments where the simulated scan "
        "and odometry data are very accurate, the scan matching algorithm can "
        "produce worse results. In those cases set this to false to improve "
        "results.",
        /* false*/ true ,
        GetParameterManager());

    /* 这个参数不知道啥意思？barycenter 是重心、质心的意思 */
	m_pUseScanBarycenter = new Parameter<kt_bool>(
        "UseScanBarycenter",
        "Use the barycenter of scan endpoints to define distances between "
        "scans.",
        true, GetParameterManager());

	/* 设置最小移动距离,如果机器人没有移动，则把 scan 忽略掉 */
    m_pMinimumTravelDistance = new Parameter<kt_double>(
        "MinimumTravelDistance",
        "Sets the minimum travel between scans.  If a new scan's position is "
        "more than minimumTravelDistance from the previous scan, the mapper "
        "will use the data from the new scan. Otherwise, it will discard the "
        "new scan if it also does not meet the minimum change in heading "
        "requirement. For performance reasons, generally it is a good idea to "
        "only process scans if the robot has moved a reasonable amount.",
        /* 0.2 */ 0.01, GetParameterManager());

	/* 设置最小方向角的改变 */
    m_pMinimumTravelHeading = new Parameter<kt_double>(
        "MinimumTravelHeading",
        "Sets the minimum heading change between scans. If a new scan's "
        "heading is more than MinimumTravelHeading from the previous scan, the "
        "mapper will use the data from the new scan.  Otherwise, it will "
        "discard the new scan if it also does not meet the minimum travel "
        "distance requirement. For performance reasons, generally it is a good "
        "idea to only process scans if the robot has moved a reasonable "
        "amount.",
        math::DegreesToRadians(/* 10 */ 10), GetParameterManager());

	/* scan buffer size 的大小  */
    m_pScanBufferSize = new Parameter<kt_int32u>(
        "ScanBufferSize",
        "Scan buffer size is the length of the scan chain stored for scan "
        "matching. \"ScanBufferSize\" should be set to approximately "
        "\"ScanBufferMaximumScanDistance\" / \"MinimumTravelDistance\". The "
        "idea is to get an area approximately 20 meters long for scan "
        "matching. For example, if we add scans every MinimumTravelDistance == "
        "0.3 meters, then \"scanBufferSize\" should be 20 / 0.3 = 67.)",
        70, GetParameterManager());

	/* 最大的 scan buffer size，主要用在 scan match */
    m_pScanBufferMaximumScanDistance = new Parameter<kt_double>(
        "ScanBufferMaximumScanDistance",
        "Scan buffer maximum scan distance is the maximum distance between the "
        "first and last scans in the scan chain stored for matching.",
        20.0, GetParameterManager());

    m_pLinkMatchMinimumResponseFine = new Parameter<kt_double>(
        "LinkMatchMinimumResponseFine",
        "Scans are linked only if the correlation response value is greater "
        "than this value.",
        0.8, GetParameterManager());

    m_pLinkScanMaximumDistance = new Parameter<kt_double>(
        "LinkScanMaximumDistance",
        "Maximum distance between linked scans.  Scans that are farther apart "
        "will not be linked regardless of the correlation response value.",
        10.0, GetParameterManager());

	/* 环路检测的距离 */
    m_pLoopSearchMaximumDistance = new Parameter<kt_double>(
        "LoopSearchMaximumDistance",
        "Scans less than this distance from the current position will be "
        "considered for a match in loop closure.",
        4.0, GetParameterManager());

	/* 是否进行环路检测 */
    m_pDoLoopClosing = new Parameter<kt_bool>(
        "DoLoopClosing",
        "Enable/disable loop closure.",
        /* true */true, GetParameterManager());

	/* 在闭环检测中最小的chain的大小  */
	m_pLoopMatchMinimumChainSize = new Parameter<kt_int32u>(
        "LoopMatchMinimumChainSize",
        "When the loop closure detection finds a candidate it must be part of "
        "a large set of linked scans. If the chain of scans is less than this "
        "value we do not attempt to close the loop.",
        /* 10 */ 10, GetParameterManager());

	/* 在闭环检测中可能解的最大方差，只有小于这个方差的才被认为可能  */
    m_pLoopMatchMaximumVarianceCoarse = new Parameter<kt_double>(
        "LoopMatchMaximumVarianceCoarse",
        "The co-variance values for a possible loop closure have to be less "
        "than this value to consider a viable solution. This applies to the "
        "coarse search.",
        math::Square(0.4), GetParameterManager());

    /* 在闭环检测中,满足闭环确认的最小响应，在较粗的范围内进行闭环检测  */
    m_pLoopMatchMinimumResponseCoarse = new Parameter<kt_double>(
        "LoopMatchMinimumResponseCoarse",
        "If response is larger then this, then initiate loop closure search at "
        "the coarse resolution.",
        /* 0.8 */ 0.7, GetParameterManager());

    /* 在闭环检测中,满足闭环确认的最小响应，在更加精细的范围内进行闭环检测  */
	m_pLoopMatchMinimumResponseFine = new Parameter<kt_double>(
        "LoopMatchMinimumResponseFine",
        "If response is larger then this, then initiate loop closure search at "
        "the fine resolution.",
        /* 0.8 */ 0.7, GetParameterManager());

    //////////////////////////////////////////////////////////////////////////////
    //    CorrelationParameters correlationParameters; 相关性参数
	/* 搜索网格的大小 */
    m_pCorrelationSearchSpaceDimension = new Parameter<kt_double>(
        "CorrelationSearchSpaceDimension",
        "The size of the search grid used by the matcher. The search grid will "
        "have the size CorrelationSearchSpaceDimension * "
        "CorrelationSearchSpaceDimension",
        0.3, GetParameterManager());

	/* 匹配时进行搜索的空间分辨率 */
    m_pCorrelationSearchSpaceResolution = new Parameter<kt_double>(
        "CorrelationSearchSpaceResolution",
        "The resolution (size of a grid cell) of the correlation grid.",
         0.01, GetParameterManager());

	/* 匹配时进行平滑处理的参数 */
    m_pCorrelationSearchSpaceSmearDeviation = new Parameter<kt_double>(
        "CorrelationSearchSpaceSmearDeviation",
        "The point readings are smeared by this value in X and Y to create a "
        "smoother response.",
        0.03, GetParameterManager());

    //////////////////////////////////////////////////////////////////////////////
    //    CorrelationParameters loopCorrelationParameters; 进行闭环检测的参数
    m_pLoopSearchSpaceDimension = new Parameter<kt_double>(
        "LoopSearchSpaceDimension",
        "The size of the search grid used by the matcher.",
        8.0, GetParameterManager());

    m_pLoopSearchSpaceResolution = new Parameter<kt_double>(
        "LoopSearchSpaceResolution",
        "The resolution (size of a grid cell) of the correlation grid.",
        0.05, GetParameterManager());

    m_pLoopSearchSpaceSmearDeviation = new Parameter<kt_double>(
        "LoopSearchSpaceSmearDeviation",
        "The point readings are smeared by this value in X and Y to create a "
        "smoother response.",
        0.03, GetParameterManager());

    //////////////////////////////////////////////////////////////////////////////
    // ScanMatcherParameters; 扫描匹配参数
    /* 距离方差的乘法系数 */
	m_pDistanceVariancePenalty = new Parameter<kt_double>(
        "DistanceVariancePenalty",
        "Variance of penalty for deviating from odometry when scan-matching. "
        "The penalty is a multiplier (less than 1.0) is a function of the "
        "delta of the scan position being tested and the odometric pose.",
        math::Square(0.3), GetParameterManager());

	/* 角度方差的乘法系数 */
    m_pAngleVariancePenalty = new Parameter<kt_double>(
        "AngleVariancePenalty",
        "See DistanceVariancePenalty.",
        math::Square(math::DegreesToRadians(20)), GetParameterManager());

	/* 角度搜索时的 offset 值 */
    m_pFineSearchAngleOffset = new Parameter<kt_double>(
        "FineSearchAngleOffset",
        "The range of angles to search during a fine search.",
        math::DegreesToRadians(0.2), GetParameterManager());
	
	/* 角度搜索时的 offset 值 */
    m_pCoarseSearchAngleOffset = new Parameter<kt_double>(
        "CoarseSearchAngleOffset",
        "The range of angles to search during a coarse search.",
        math::DegreesToRadians(20), GetParameterManager());

	/* 角度搜索时分辨率 */
	m_pCoarseAngleResolution = new Parameter<kt_double>(
        "CoarseAngleResolution",
        "Resolution of angles to search during a coarse search.",
        math::DegreesToRadians(2), GetParameterManager());

	/* 角度搜索时最小惩罚值 */
    m_pMinimumAnglePenalty = new Parameter<kt_double>(
        "MinimumAnglePenalty",
        "Minimum value of the angle penalty multiplier so scores do not become "
        "too small.",
        0.9, GetParameterManager());

	/* 距离搜索时最小惩罚值 */
    m_pMinimumDistancePenalty = new Parameter<kt_double>(
        "MinimumDistancePenalty",
        "Minimum value of the distance penalty multiplier so scores do not "
        "become too small.",
        0.5, GetParameterManager());

	/* 如果没有搜索最佳匹配时，是否增加搜索空间 */
    m_pUseResponseExpansion = new Parameter<kt_bool>(
        "UseResponseExpansion",
        "Whether to increase the search space if no good matches are initially "
        "found.",
        false, GetParameterManager());
  }
  /* Adding in getters and setters here for easy parameter access */

  // General Parameters

  bool Mapper::getParamUseScanMatching()
  {
    return static_cast<bool>(m_pUseScanMatching->GetValue());
  }

  bool Mapper::getParamUseScanBarycenter()
  {
    return static_cast<bool>(m_pUseScanBarycenter->GetValue());
  }

  double Mapper::getParamMinimumTravelDistance()
  {
    return static_cast<double>(m_pMinimumTravelDistance->GetValue());
  }

  double Mapper::getParamMinimumTravelHeading()
  {
    return math::RadiansToDegrees(static_cast<double>(m_pMinimumTravelHeading->GetValue()));
  }

  int Mapper::getParamScanBufferSize()
  {
    return static_cast<int>(m_pScanBufferSize->GetValue());
  }

  double Mapper::getParamScanBufferMaximumScanDistance()
  {
    return static_cast<double>(m_pScanBufferMaximumScanDistance->GetValue());
  }

  double Mapper::getParamLinkMatchMinimumResponseFine()
  {
    return static_cast<double>(m_pLinkMatchMinimumResponseFine->GetValue());
  }

  double Mapper::getParamLinkScanMaximumDistance()
  {
    return static_cast<double>(m_pLinkScanMaximumDistance->GetValue());
  }

  double Mapper::getParamLoopSearchMaximumDistance()
  {
    return static_cast<double>(m_pLoopSearchMaximumDistance->GetValue());
  }

  bool Mapper::getParamDoLoopClosing()
  {
    return static_cast<bool>(m_pDoLoopClosing->GetValue());
  }

  int Mapper::getParamLoopMatchMinimumChainSize()
  {
    return static_cast<int>(m_pLoopMatchMinimumChainSize->GetValue());
  }

  double Mapper::getParamLoopMatchMaximumVarianceCoarse()
  {
    return static_cast<double>(std::sqrt(m_pLoopMatchMaximumVarianceCoarse->GetValue()));
  }

  double Mapper::getParamLoopMatchMinimumResponseCoarse()
  {
    return static_cast<double>(m_pLoopMatchMinimumResponseCoarse->GetValue());
  }

  double Mapper::getParamLoopMatchMinimumResponseFine()
  {
    return static_cast<double>(m_pLoopMatchMinimumResponseFine->GetValue());
  }

  // Correlation Parameters - Correlation Parameters

  double Mapper::getParamCorrelationSearchSpaceDimension()
  {
    return static_cast<double>(m_pCorrelationSearchSpaceDimension->GetValue());
  }

  double Mapper::getParamCorrelationSearchSpaceResolution()
  {
    return static_cast<double>(m_pCorrelationSearchSpaceResolution->GetValue());
  }

  double Mapper::getParamCorrelationSearchSpaceSmearDeviation()
  {
    return static_cast<double>(m_pCorrelationSearchSpaceSmearDeviation->GetValue());
  }

  // Correlation Parameters - Loop Correlation Parameters

  double Mapper::getParamLoopSearchSpaceDimension()
  {
    return static_cast<double>(m_pLoopSearchSpaceDimension->GetValue());
  }

  double Mapper::getParamLoopSearchSpaceResolution()
  {
    return static_cast<double>(m_pLoopSearchSpaceResolution->GetValue());
  }

  double Mapper::getParamLoopSearchSpaceSmearDeviation()
  {
    return static_cast<double>(m_pLoopSearchSpaceSmearDeviation->GetValue());
  }

  // ScanMatcher Parameters

  double Mapper::getParamDistanceVariancePenalty()
  {
    return std::sqrt(static_cast<double>(m_pDistanceVariancePenalty->GetValue()));
  }

  double Mapper::getParamAngleVariancePenalty()
  {
    return std::sqrt(static_cast<double>(m_pAngleVariancePenalty->GetValue()));
  }

  double Mapper::getParamFineSearchAngleOffset()
  {
    return static_cast<double>(m_pFineSearchAngleOffset->GetValue());
  }

  double Mapper::getParamCoarseSearchAngleOffset()
  {
    return static_cast<double>(m_pCoarseSearchAngleOffset->GetValue());
  }

  double Mapper::getParamCoarseAngleResolution()
  {
    return static_cast<double>(m_pCoarseAngleResolution->GetValue());
  }

  double Mapper::getParamMinimumAnglePenalty()
  {
    return static_cast<double>(m_pMinimumAnglePenalty->GetValue());
  }

  double Mapper::getParamMinimumDistancePenalty()
  {
    return static_cast<double>(m_pMinimumDistancePenalty->GetValue());
  }

  bool Mapper::getParamUseResponseExpansion()
  {
    return static_cast<bool>(m_pUseResponseExpansion->GetValue());
  }

  /* Setters for parameters */
  // General Parameters
  void Mapper::setParamUseScanMatching(bool b)
  {
    m_pUseScanMatching->SetValue((kt_bool)b);
  }

  void Mapper::setParamUseScanBarycenter(bool b)
  {
    m_pUseScanBarycenter->SetValue((kt_bool)b);
  }

  void Mapper::setParamMinimumTravelDistance(double d)
  {
    m_pMinimumTravelDistance->SetValue((kt_double)d);
  }

  void Mapper::setParamMinimumTravelHeading(double d)
  {
    m_pMinimumTravelHeading->SetValue((kt_double)d);
  }

  void Mapper::setParamScanBufferSize(int i)
  {
    m_pScanBufferSize->SetValue((kt_int32u)i);
  }

  void Mapper::setParamScanBufferMaximumScanDistance(double d)
  {
    m_pScanBufferMaximumScanDistance->SetValue((kt_double)d);
  }

  void Mapper::setParamLinkMatchMinimumResponseFine(double d)
  {
    m_pLinkMatchMinimumResponseFine->SetValue((kt_double)d);
  }

  void Mapper::setParamLinkScanMaximumDistance(double d)
  {
    m_pLinkScanMaximumDistance->SetValue((kt_double)d);
  }

  void Mapper::setParamLoopSearchMaximumDistance(double d)
  {
    m_pLoopSearchMaximumDistance->SetValue((kt_double)d);
  }

  void Mapper::setParamDoLoopClosing(bool b)
  {
    m_pDoLoopClosing->SetValue((kt_bool)b);
  }

  void Mapper::setParamLoopMatchMinimumChainSize(int i)
  {
    m_pLoopMatchMinimumChainSize->SetValue((kt_int32u)i);
  }

  void Mapper::setParamLoopMatchMaximumVarianceCoarse(double d)
  {
    m_pLoopMatchMaximumVarianceCoarse->SetValue((kt_double)math::Square(d));
  }

  void Mapper::setParamLoopMatchMinimumResponseCoarse(double d)
  {
    m_pLoopMatchMinimumResponseCoarse->SetValue((kt_double)d);
  }

  void Mapper::setParamLoopMatchMinimumResponseFine(double d)
  {
    m_pLoopMatchMinimumResponseFine->SetValue((kt_double)d);
  }

  // Correlation Parameters - Correlation Parameters
  void Mapper::setParamCorrelationSearchSpaceDimension(double d)
  {
    m_pCorrelationSearchSpaceDimension->SetValue((kt_double)d);
  }

  void Mapper::setParamCorrelationSearchSpaceResolution(double d)
  {
    m_pCorrelationSearchSpaceResolution->SetValue((kt_double)d);
  }

  void Mapper::setParamCorrelationSearchSpaceSmearDeviation(double d)
  {
    m_pCorrelationSearchSpaceSmearDeviation->SetValue((kt_double)d);
  }


  // Correlation Parameters - Loop Closure Parameters
  void Mapper::setParamLoopSearchSpaceDimension(double d)
  {
    m_pLoopSearchSpaceDimension->SetValue((kt_double)d);
  }

  void Mapper::setParamLoopSearchSpaceResolution(double d)
  {
    m_pLoopSearchSpaceResolution->SetValue((kt_double)d);
  }

  void Mapper::setParamLoopSearchSpaceSmearDeviation(double d)
  {
    m_pLoopSearchSpaceSmearDeviation->SetValue((kt_double)d);
  }


  // Scan Matcher Parameters
  void Mapper::setParamDistanceVariancePenalty(double d)
  {
    m_pDistanceVariancePenalty->SetValue((kt_double)math::Square(d));
  }

  void Mapper::setParamAngleVariancePenalty(double d)
  {
    m_pAngleVariancePenalty->SetValue((kt_double)math::Square(d));
  }

  void Mapper::setParamFineSearchAngleOffset(double d)
  {
    m_pFineSearchAngleOffset->SetValue((kt_double)d);
  }

  void Mapper::setParamCoarseSearchAngleOffset(double d)
  {
    m_pCoarseSearchAngleOffset->SetValue((kt_double)d);
  }

  void Mapper::setParamCoarseAngleResolution(double d)
  {
    m_pCoarseAngleResolution->SetValue((kt_double)d);
  }

  void Mapper::setParamMinimumAnglePenalty(double d)
  {
    m_pMinimumAnglePenalty->SetValue((kt_double)d);
  }

  void Mapper::setParamMinimumDistancePenalty(double d)
  {
    m_pMinimumDistancePenalty->SetValue((kt_double)d);
  }

  void Mapper::setParamUseResponseExpansion(bool b)
  {
    m_pUseResponseExpansion->SetValue((kt_bool)b);
  }



  void Mapper::Initialize(kt_double rangeThreshold)
  {
    if (m_Initialized == false)
    {
      // create sequential scan and loop matcher 创建一个序列扫描器和闭环检测器
      m_pSequentialScanMatcher = ScanMatcher::Create(this,
                                                    m_pCorrelationSearchSpaceDimension->GetValue(), // 0.3
                                                    m_pCorrelationSearchSpaceResolution->GetValue(), // 0.01
                                                    m_pCorrelationSearchSpaceSmearDeviation->GetValue(), // 0.03
                                                    rangeThreshold);
      assert(m_pSequentialScanMatcher);

	  /* 创建一个扫描设备管理器 */
      m_pMapperSensorManager = new MapperSensorManager(m_pScanBufferSize->GetValue(),
                                                       m_pScanBufferMaximumScanDistance->GetValue());

      m_pGraph = new MapperGraph(this, rangeThreshold);

      m_Initialized = true;
    }
  }

  void Mapper::Reset()
  {
    delete m_pSequentialScanMatcher;
    m_pSequentialScanMatcher = NULL;

    delete m_pGraph;
    m_pGraph = NULL;

    delete m_pMapperSensorManager;
    m_pMapperSensorManager = NULL;

    m_Initialized = false;
  }

  kt_bool Mapper::Process(Object*  /*pObject*/)
  {
    return true;
  }

  /* 对激光数据进行处理 */
  kt_bool Mapper::Process(LocalizedRangeScan* pScan)
  {
    if (pScan != NULL)
    {
      karto::LaserRangeFinder* pLaserRangeFinder = pScan->GetLaserRangeFinder();

      // validate scan
	  /* pLaserRangeFinder->Validate(pScan) 只是进行了简单的验证 */
      if (pLaserRangeFinder == NULL || pScan == NULL || pLaserRangeFinder->Validate(pScan) == false)
      {
        return false;
      }

      if (m_Initialized == false)
      {
        // initialize mapper with range threshold from device
        /* 第一次运行的时候需要做初始化操作, 初始化m_pSequentialScanMatcher和m_pMapperSensorManager */
        Initialize(pLaserRangeFinder->GetRangeThreshold());
      }

      // get last scan 获取最新的扫描
      LocalizedRangeScan* pLastScan = m_pMapperSensorManager->GetLastScan(pScan->GetSensorName());

	  Pose2 ConPos;

      // update scans corrected pose based on last correction
      if (pLastScan != NULL)
      {
        /* 定义了一个变换，从里程计姿态到正确姿态的转换 */
        Transform lastTransform(pLastScan->GetOdometricPose(), pLastScan->GetCorrectedPose());
        /* 利用以前的变换对当前的扫描线进行变换 */
		ConPos = lastTransform.TransformPose(pScan->GetOdometricPose());

        pScan->SetCorrectedPose(ConPos);
      }

      // test if scan is outside minimum boundary or if heading is larger then minimum heading
      if (!HasMovedEnough(pScan, pLastScan))  /* 前后两个 scan 是否具有足够的距离（包括角度和空间），如果足够远，则进行匹配，否则直接返回 */
      {
        //std::cout << "Has not moved enough!" << std::endl;
        return false;
      }

      Matrix3 covariance; /* 定义协方差矩阵 */
      covariance.SetToIdentity(); /* 设置成单位矩阵 */

      // correct scan (if not first scan)
      if (m_pUseScanMatching->GetValue() && pLastScan != NULL)
      {
        Pose2 bestPose;

		kt_double bestResponse;

		/* 进行扫描匹配 */
		bestResponse = m_pSequentialScanMatcher->MatchScan(pScan,
                                            m_pMapperSensorManager->GetRunningScans(pScan->GetSensorName()),
                                                                                    bestPose,
                                                                                    covariance);

		pScan->SetSensorPose(bestPose);

      }

      //std::cout << covariance(0, 0) << " " << covariance(0, 1) << " " << covariance(0, 2) << std::endl;
      //std::cout << covariance(1, 0) << " " << covariance(1, 1) << " " << covariance(1, 2) << std::endl;
      //std::cout << covariance(2, 0) << " " << covariance(2, 1) << " " << covariance(2, 2) << std::endl;

      // add scan to buffer and assign id 将扫描数据添加到缓冲区中
      m_pMapperSensorManager->AddScan(pScan);

      if (m_pUseScanMatching->GetValue())
      {
        // add to graph
        //std::cout << "add to graph..." << std::endl;
        m_pGraph->AddVertex(pScan);
        m_pGraph->AddEdges(pScan, covariance);

        m_pMapperSensorManager->AddRunningScan(pScan);

        if (m_pDoLoopClosing->GetValue())
        {
            std::vector<Name> deviceNames = m_pMapperSensorManager->GetSensorNames();
            const_forEach(std::vector<Name>, &deviceNames)
            {
                kt_bool isLoop = false;
                isLoop = m_pGraph->TryCloseLoop(pScan, *iter);
                if(isLoop)
                {
                    std::cout << "Loop closed........................................" << std::endl;
                    //cv::waitKey(0);
                }
            }
        }
      }

	  /* 设置最后一次扫描数据 */
      m_pMapperSensorManager->SetLastScan(pScan);

      return true;
    }

    return false;
  }

  /**
   * Is the scan sufficiently far from the last scan?
   * @param pScan
   * @param pLastScan
   * @return true if the scans are sufficiently far
   */
  kt_bool Mapper::HasMovedEnough(LocalizedRangeScan* pScan, LocalizedRangeScan* pLastScan) const
  {
    // test if first scan
    if (pLastScan == NULL)
    {
      return true;
    }

    Pose2 lastScannerPose = pLastScan->GetSensorAt(pLastScan->GetOdometricPose());
    Pose2 scannerPose = pScan->GetSensorAt(pScan->GetOdometricPose());

    // test if we have turned enough
    kt_double deltaHeading = math::NormalizeAngle(scannerPose.GetHeading() - lastScannerPose.GetHeading());
    if (fabs(deltaHeading) >= m_pMinimumTravelHeading->GetValue())
    {
      return true;
    }

    // test if we have moved enough
    kt_double squaredTravelDistance = lastScannerPose.GetPosition().SquaredDistance(scannerPose.GetPosition());
    if (squaredTravelDistance >= math::Square(m_pMinimumTravelDistance->GetValue()) - KT_TOLERANCE)
    {
      return true;
    }

    return false;
  }

  /**
   * Gets all the processed scans 返回所有的扫描数据
   * @return all scans
   */
  const LocalizedRangeScanVector Mapper::GetAllProcessedScans() const
  {
    LocalizedRangeScanVector allScans;

    if (m_pMapperSensorManager != NULL)
    {
      allScans = m_pMapperSensorManager->GetAllScans();
    }

    return allScans;
  }

  /**
   * Adds a listener
   * @param pListener
   */
  void Mapper::AddListener(MapperListener* pListener)
  {
    m_Listeners.push_back(pListener);
  }

  /**
   * Removes a listener
   * @param pListener
   */
  void Mapper::RemoveListener(MapperListener* pListener)
  {
    std::vector<MapperListener*>::iterator iter = std::find(m_Listeners.begin(), m_Listeners.end(), pListener);
    if (iter != m_Listeners.end())
    {
      m_Listeners.erase(iter);
    }
  }

  void Mapper::FireInfo(const std::string& rInfo) const
  {
    const_forEach(std::vector<MapperListener*>, &m_Listeners)
    {
      (*iter)->Info(rInfo);
    }
  }

  void Mapper::FireDebug(const std::string& rInfo) const
  {
    const_forEach(std::vector<MapperListener*>, &m_Listeners)
    {
      MapperDebugListener* pListener = dynamic_cast<MapperDebugListener*>(*iter);

      if (pListener != NULL)
      {
        pListener->Debug(rInfo);
      }
    }
  }

  void Mapper::FireLoopClosureCheck(const std::string& rInfo) const
  {
    const_forEach(std::vector<MapperListener*>, &m_Listeners)
    {
      MapperLoopClosureListener* pListener = dynamic_cast<MapperLoopClosureListener*>(*iter);

      if (pListener != NULL)
      {
        pListener->LoopClosureCheck(rInfo);
      }
    }
  }

  void Mapper::FireBeginLoopClosure(const std::string& rInfo) const
  {
    const_forEach(std::vector<MapperListener*>, &m_Listeners)
    {
      MapperLoopClosureListener* pListener = dynamic_cast<MapperLoopClosureListener*>(*iter);

      if (pListener != NULL)
      {
        pListener->BeginLoopClosure(rInfo);
      }
    }
  }

  void Mapper::FireEndLoopClosure(const std::string& rInfo) const
  {
    const_forEach(std::vector<MapperListener*>, &m_Listeners)
    {
      MapperLoopClosureListener* pListener = dynamic_cast<MapperLoopClosureListener*>(*iter);

      if (pListener != NULL)
      {
        pListener->EndLoopClosure(rInfo);
      }
    }
  }

  void Mapper::SetScanSolver(ScanSolver* pScanOptimizer)
  {
    m_pScanOptimizer = pScanOptimizer;
  }

  MapperGraph* Mapper::GetGraph() const
  {
    return m_pGraph;
  }

  ScanMatcher* Mapper::GetSequentialScanMatcher() const
  {
    return m_pSequentialScanMatcher;
  }

  ScanMatcher* Mapper::GetLoopScanMatcher() const
  {
    return m_pGraph->GetLoopScanMatcher();
  }
}  // namespace karto
