//
//  my_map_creator.cpp
//  cocos2d_js_bindings
//
//  Created by luleyan on 2018/12/12.
//

#include "my_map_creator.hpp"
#include <thread>
#include <random>

#include "scripting/js-bindings/auto/jsb_cocos2dx_auto.hpp"
#include "scripting/js-bindings/manual/jsb_conversions.hpp"
#include "scripting/js-bindings/manual/jsb_global.h"
#include "cocos2d.h"
#include "scripting/js-bindings/manual/BaseJSAction.h"

#include "json/document.h"
#include "json/stringbuffer.h"
#include "json/writer.h"

#define MY_SPACE_BEGIN   namespace my {
#define MY_SPACE_END     }

USING_NS_CC;

// 测试函数 ---------------------------------------------------------------

static int64_t getCurrentTime() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static void printVecVec(std::vector<std::vector<int>> &vecvec, int d = 5) {
    printf("vvvvvvvvvvvvvvv\n");

    char c[1];
    sprintf(c, "%d", d);
    std::string format = std::string("%0") + c + "d, ";
    for (int i = 0; i < vecvec.size(); i++) {
        std::vector<int> vec = vecvec[i];
        for (int j = 0; j < vec.size(); j++) {
            printf(format.c_str(), vec[j]);
        }
        printf("\n");
    }
    printf("^^^^^^^^^^^^^^^\n");
}

static void printVecVecToFile(std::vector<std::vector<int>> &vecvec, std::string path) {
    std::string vecStr = "";
    for (int i = 0; i < vecvec.size(); i++) {
        std::vector<int> vec = vecvec[i];
        for (int j = 0; j < vec.size(); j++) {
            char c[10];
            sprintf(c, "%d", vec[j]);
            vecStr += c;
            vecStr += ",";
        }
        vecStr += "\n";
    }
    
    auto filepath = FileUtils::getInstance()->getWritablePath();
    filepath.append(path);
    printf("save to: %s", filepath.c_str());
    FILE* file = fopen(filepath.c_str(), "w");
    if(file) {
        fputs(vecStr.c_str(), file);
        fclose(file);
        printf("\n");
    } else {
        printf(" >> ERROR! \n");
    }
}

// 获取随机数 -----------------------------------------------------------

static std::default_random_engine* randomEngine = nullptr;

static void getReadyForRandom() {
    if (!randomEngine) {
        randomEngine = new std::default_random_engine();
    }

    // 用不确定的时间做seed
    int timeSeed = (int)time(0);

    // 用不确定的内存地址做seed
    int* p = new int(627);
    void* pp = &p;
    int ptrSeed = *(int*)pp;
    delete p;

    int seed = abs(timeSeed - abs(ptrSeed));
    printf("s = %d (%d, %d)\n", seed, timeSeed, ptrSeed);

    randomEngine->seed(1557623694);
}

static inline int getRandom(int from, int to) {
    return ((*randomEngine)() % (to - from + 1)) + from;
}

static inline bool ifInPercent(int percent) {
    return getRandom(1, 100) < percent;
}

// 数据模型 ---------------------------------------------------------------

MY_SPACE_BEGIN

// 从js层获得的数据 -----------------------------------------------------------

// 随机区域中使用的区块模板的基础块
class MapEleBase {
public:
    MapEleBase();
    virtual ~MapEleBase();

    int tW;
    int tH;

    std::vector<std::vector<int>> co; // 碰撞 ele的地形和碰撞数据中数字是一致的，所以有一个即可
};

class SpineData {
public:
    SpineData();
    virtual ~SpineData();
    
    int pX;
    int pY;
    int id;
};

// 随机瓦块替换
class TileSubst {
public:
    TileSubst();
    virtual ~TileSubst();
    
    int origin; // 原瓦块
    std::vector<int> substs; // 替换成的瓦块
    int ratio; // 替换百分比
};

// 区域的属性
class AreaAttri {
public:
    AreaAttri();
    virtual ~AreaAttri();
    
    int holeRatio; // hole占总地图的百分比
    std::vector<TileSubst*> tileSubsts;
};

// 随机区域中使用的区块模板，从Base中扣出来
class MapEle {
public:
    MapEle();
    virtual ~MapEle();

    int baseIndex; // base序号

    int tW;
    int tH;
    int usingTXs; // 横向使用的块 如 110011，就是使用两边各两个
    int usingTYs; // 纵向使用的块

    std::vector<int> door[4]; // 上，下，左，右 的固定块的连接方向，值对应从左到右从上到下，从0开始的tX/tY
    
    std::vector<SpineData*> spineList;
};

#define MAX_R_TW (7)
#define MAX_R_TH (6)
#define MAX_DOOR_TYPE (15) // 1门4个 2门6个（左上，右上，左下，右下，左右，上下） 3门4个（左上右，上右下，右下左，下左上） 4门1个

enum class EleDoorType {
    lef, top, rig, bot,
    lef_top, rig_top, lef_bot, rig_bot, lef_rig, top_bot,
    lef_top_rig, top_rig_bot, rig_bot_lef, bot_lef_top,
    all
};

// 元素清单，记录了不同宽高，不同门方向可以使用的元素
class MapEleList {
public:
    MapEleList();
    virtual ~MapEleList();

    // 对应不同w，h以及门方向的随机区块使用的配置，最大宽高是固定的
    // 最后的2是分成能在场景1-0使用的和不能在场景1-0使用的
    std::vector<int> list[MAX_R_TW][MAX_R_TH][MAX_DOOR_TYPE][2];
};

// 模板中的固定块
class FiTemp {
public:
    FiTemp();
    virtual ~FiTemp();

    int rX;
    int rY;
    int rW;
    int rH;

    int tX;
    int tY;
    int tW;
    int tH;

    std::vector<std::vector<int>> te; // 地形
    std::vector<std::vector<int>> co; // 碰撞
    std::vector<int> door[4]; // 上，下，左，右 的固定块的连接方向
    int substitutes[4]; // 上，下，左，右 如果不能连接，则替代的方向0-3
};

#define NO_ENEMY_KEY (1000)

// 地图区域模板
class AreaTemp {
public:
    AreaTemp();
    virtual ~AreaTemp();

    int rW;
    int rH;

    std::vector<int> noeps; // 无敌人位置（***每个int 为 x * NO_ENEMY_KEY + y 组成）
    std::vector<FiTemp*> fis; // 固定块
    std::vector<std::vector<int>> ra; // 随机块
    
    std::vector<SpineData*> spineList;
    AreaAttri* areaAttri;

    int getNoEnemyData(int x, int y) {
        return y * NO_ENEMY_KEY + x;
    }
};

// 要发送到js的数据 -----------------------------------------------------------

class FinalAreaData {
public:
    FinalAreaData();
    virtual ~FinalAreaData();

    std::vector<std::vector<int>> te; // 地形
    std::vector<std::vector<int>> co; // 碰撞
    std::vector<int> groundInfos; // 地面信息（***每3个int一组，分别是x，y，type）
    
    std::vector<SpineData*> spineList;
};

// 临时数据 ------------------------------------------------------------------

// 门方向索引，可以有多个方向所以用二进制
static const int DOOR_UP = 1 << 0;
static const int DOOR_DOWN = 1 << 1;
static const int DOOR_LEFT = 1 << 2;
static const int DOOR_RIGHT = 1 << 3;

// 方向
enum class HoleDir {
    lef_top,
    lef_mid,
    lef_bot,
    mid_top,
    mid_bot,
    rig_top,
    rig_mid,
    rig_bot,
};

enum class HoleDirOffsetType {
    none,
    left_or_up,
    right_or_down,
    full,
};

class PipeEndPoint {
public:
    PipeEndPoint();
    virtual ~PipeEndPoint();

    int holeIndex;
    int dir; // 等于对应hole的doorDir
    int tX;
    int tY;
};

// 连接hole的通道
class PipeData {
public:
    PipeData();
    virtual ~PipeData();

    PipeEndPoint* endPoints[2];

    // 管道的坐标
    std::vector<int> tXs;
    std::vector<int> tYs;

    bool connected; // 默认为true，但有的管子可以不通
};

// 标记其他hole的位置关系
class HoleRelation {
public:
    HoleRelation();
    virtual ~HoleRelation();

    HoleDir dir;
    HoleDirOffsetType offsetType;
    float distance;

    int myHoleIndex;
    int anoHoleIndex;

    int pipeIndex;
};

enum class HoleType {
    fi,
    ra,
};

class HoleData{
public:
    HoleData(int itX, int itY, int itW, int itH, HoleType itype, int iindex);
    virtual ~HoleData();

    int tX;
    int tY;
    int tW;
    int tH;

    HoleType type;
    int index; // 在vec中的位置
    int fiIndex; // 如果是fi，则其在fi list中的位置

    bool inCircuit; // 是否在通路中

    int doorDir; // 二进制 每一位代表一个方向是否存在门
    std::map<int, int> substitutesMap; // HoleDir: HoleDir

    std::vector<HoleRelation*> relations;

    MapEle* ele;
};

class AreaTmpData {
public:
    AreaTmpData();
    virtual ~AreaTmpData();

    FinalAreaData* finalAreaData;

    int tW;
    int tH;

    std::vector<std::vector<int>> thumbArea;
    std::vector<HoleData*> holeVec;
    std::vector<PipeData*> pipeVec;

    int curSceneKey;
    AreaTemp* w_curTemp; // 弱引用的当前地图模板
};

// 地图生成器 ----------------------------------------------------------------

// thumb area的值
#define FI_HOLE_ID_BEGIN (1000)
#define RA_HOLE_ID_BEGIN (2000)
#define FI_EDGE_ID_BEGIN (100)
#define RA_EDGE_ID_BEGIN (200)
#define PIPE_ID_BEGIN (10000)
#define PIPE_FIRST_BLOCK_ID (20000) // pipe的头的id从这个开始

#define PIPE_TYPE_BLANK (30000)
#define PIPE_TYPE_0 (40000)
#define PIPE_TYPE_1 (50000)
#define PIPE_TYPE_2 (60000)
#define PIPE_TYPE_0_BLOCK (70000)
#define PIPE_TYPE_1_BLOCK (80000)
#define PIPE_TYPE_2_BLOCK (90000)
#define USING_WALL_LEFT (100000) // 移除的边向左
#define USING_WALL_RIGHT (110000)

// pipe中每个块的类型
enum class PipeBlockType {
    blank, // 全空
    plat0, // 第一行有平台
    plat1, // 第二行有平台
    plat2, // 第三行有平台
    plat02, // 第一，三行有平台
};

// tilemap中的地形和碰撞
static const int MAP_CO_DATA_BLANK = 0;
static const int MAP_CO_DATA_BLOCK = 1;
static const int MAP_CO_DATA_BLOCK_UP = 3;

static const int MAP_CO_DATA_PLAT = 19;

static const int MAP_CO_DATA_PLAT_HEAD_L = 20;
static const int MAP_CO_DATA_PLAT_HEAD = 21;
static const int MAP_CO_DATA_PLAT_HEAD_R = 22;

static const int MAP_CO_DATA_PLAT_BG_L = 23;
static const int MAP_CO_DATA_PLAT_BG = 24;
static const int MAP_CO_DATA_PLAT_BG_R = 25;

static const int MAP_AUTO_TE_DATA_MAX = 32; // 最大的自动地形，<=这个值的地形都是跟着碰撞走的，>的属于自定义

static const int TileLength = 32;

class MapCreator {
public:
    MapCreator();
    virtual ~MapCreator();

    static MapCreator* getInstance();

    // 载入区块元素
    void addMapEleBase(const MapEleBase* mapEleBase);
    void addMapEle(const MapEle* mapEle);
    void addMapEleIndexs(const int tW, const int tH, const int doorType, const int sceneKey, std::vector<int> &eleIndexs);

    // 读取模板
    void addAreaTemp(const int sceneKey, const AreaTemp* areaTemp);

    // 生成地图，然后从回调传出
    void createArea(const int sceneKey, const std::function<void(bool)>& callback);

protected:
    void init();
    void threadLoop();

    void initTmpData(AreaTmpData* tmpData);
    void digHole(AreaTmpData* tmpData);
    void calcHoleRelation(AreaTmpData* tmpData);
    void connectAllHole(AreaTmpData* tmpData);
    void connectExtraHole(AreaTmpData* tmpData);
    void assignEleToHole(AreaTmpData* tmpData);
    void digPipe(AreaTmpData* tmpData);
    
    void calcSpines(AreaTmpData* tmpData);
    void createFinalArea(AreaTmpData* tmpData);
    void finishFinalArea(AreaTmpData* tmpData);
    
    void handleGround(AreaTmpData* tmpData);
    void createExtraPipeSpine(AreaTmpData* tmpData);
    void addRandomTile(AreaTmpData* tmpData);

    void saveToJsonFile(AreaTmpData* tmpData);

private:
    bool _creating;

    std::thread _thread;
    std::mutex _sleepMutex;
    std::condition_variable _sleepCondition;

    // 输入数据
    std::vector<MapEleBase*> _mapEleBaseVec;
    std::vector<MapEle*> _mapEleVec;
    MapEleList _mapEleList; // 不同场景Key对应的元素清单

    std::map<int, AreaTemp*> _areaTempMap; // 不同场景Key对应的地图模板

    int _curSceneKey;
    std::function<void(bool)> _callback;
};

// 实现 --------------------------------------------------------------

MapEleBase::MapEleBase() {
}

MapEleBase::~MapEleBase() {
}

// ---------------

SpineData::SpineData() {
}

SpineData::~SpineData() {
}

// ---------------

TileSubst::TileSubst() {
}

TileSubst::~TileSubst() {
}

// ---------------

AreaAttri::AreaAttri() {
}

AreaAttri::~AreaAttri() {
    for (TileSubst* tileSubst : tileSubsts) {
        delete tileSubst;
    }
}

// ---------------

MapEle::MapEle() {
}

MapEle::~MapEle() {
    for (SpineData* spineData: spineList) {
        delete spineData;
    }
}

// ---------------

MapEleList::MapEleList() {
}

MapEleList::~MapEleList() {
}

// ---------------

FiTemp::FiTemp() {
}

FiTemp::~FiTemp() {
}

// ---------------

AreaTemp::AreaTemp() {
}

AreaTemp::~AreaTemp() {
    for (FiTemp* fi: fis) {
        delete fi;
    }
    
    for (SpineData* spineData: spineList) {
        delete spineData;
    }
    
    delete areaAttri;
}

// ---------------

FinalAreaData::FinalAreaData() {
}

FinalAreaData::~FinalAreaData() {
    for (SpineData* spineData : spineList) {
        delete spineData;
    }
}

// ---------------

PipeEndPoint::PipeEndPoint() {
}

PipeEndPoint::~PipeEndPoint() {
}

// ---------------

PipeData::PipeData(): connected(true) {
    endPoints[0] = nullptr;
    endPoints[1] = nullptr;
}

PipeData::~PipeData() {
    if (endPoints[0]) delete endPoints[0];
    if (endPoints[1]) delete endPoints[1];
}

// ---------------

HoleRelation::HoleRelation():
pipeIndex(-1) {
}

HoleRelation::~HoleRelation() {
}

// ---------------

HoleData::HoleData(int itX, int itY, int itW, int itH, HoleType itype, int iindex):
inCircuit(false), doorDir(0), tX(itX), tY(itY), tW(itW), tH(itH), type(itype), index(iindex), fiIndex(-1) {
}

HoleData::~HoleData() {
    for (HoleRelation* r : relations) {
        delete r;
    }
}

AreaTmpData::AreaTmpData() {
    finalAreaData = new FinalAreaData();
}

AreaTmpData::~AreaTmpData() {
    for (HoleData* hole: holeVec) {
        delete hole;
    }
    for (PipeData* pipe: pipeVec) {
        delete pipe;
    }

    delete finalAreaData;
}

// ---------------

static MapCreator *s_MapCreator = nullptr;

MapCreator::MapCreator():
_creating(false) {
    getReadyForRandom();
}

MapCreator::~MapCreator() {
    for (MapEleBase* eleBase : _mapEleBaseVec) {
        delete eleBase;
    }

    for (MapEle* ele: _mapEleVec) {
        delete ele;
    }

    std::map<int, AreaTemp*>::iterator it2;
    for(it2 = _areaTempMap.begin(); it2 != _areaTempMap.end();) {
        delete it2->second;
        _areaTempMap.erase(it2++);
    }
}

MapCreator* MapCreator::getInstance() {
    if (!s_MapCreator) {
        s_MapCreator = new (std::nothrow) MapCreator();
        s_MapCreator->init();
    }
    return s_MapCreator;
}

void MapCreator::addMapEleBase(const MapEleBase* mapEleBase) {
    _mapEleBaseVec.push_back(const_cast<MapEleBase*>(mapEleBase));
}

void MapCreator::addMapEle(const MapEle* mapEle) {
    _mapEleVec.push_back(const_cast<MapEle*>(mapEle));
}

void MapCreator::addMapEleIndexs(const int tW, const int tH, const int doorType, const int sceneKey, std::vector<int> &eleIndexs) {
    assert(0 <= tW && tW < MAX_R_TW);
    assert(0 <= tH && tH < MAX_R_TH);
    assert(sceneKey == 0 || sceneKey == 1); // 0,1 分别表示用在场景1-0的和用在其他场景的
    auto ptr = &_mapEleList.list[tW][tH][doorType][sceneKey];
    ptr->insert(ptr->end(), eleIndexs.begin(), eleIndexs.end());
}

void MapCreator::addAreaTemp(const int sceneKey, const AreaTemp* areaTemp) {
    _areaTempMap[sceneKey] = const_cast<AreaTemp*>(areaTemp);
}

void MapCreator::createArea(const int sceneKey, const std::function<void(bool)>& callback) {
    if (_creating) return;
    _creating = true;

    _curSceneKey = sceneKey;
    _callback = callback;

    std::unique_lock<std::mutex> lk(_sleepMutex);
    _sleepCondition.notify_one();
}

void MapCreator::init() {
    // 初始化线程
    _thread = std::thread(&MapCreator::threadLoop, this);
    _thread.detach();
}

void MapCreator::threadLoop() {
    while (true) {
        std::unique_lock<std::mutex> lk(_sleepMutex);
        _sleepCondition.wait(lk);

        log("begin to create map");

        AreaTmpData* tmpData = new AreaTmpData();

        tmpData->curSceneKey = _curSceneKey;
        tmpData->w_curTemp = _areaTempMap[_curSceneKey];

        initTmpData(tmpData);
        digHole(tmpData);
        calcHoleRelation(tmpData);
        connectAllHole(tmpData);
        connectExtraHole(tmpData);
        assignEleToHole(tmpData);
        digPipe(tmpData);
        
        calcSpines(tmpData);
        createFinalArea(tmpData);
        finishFinalArea(tmpData);
        
        handleGround(tmpData);
        createExtraPipeSpine(tmpData);
        addRandomTile(tmpData);

        // mapData 保存到本地 todo
        saveToJsonFile(tmpData);

        // 释放
        delete tmpData;

        // 结束
        auto sch = cocos2d::Director::getInstance()->getScheduler();
        sch->performFunctionInCocosThread([=]() {
            _creating = false;
            _callback(true);
        });
    }
}

void MapCreator::initTmpData(AreaTmpData* tmpData) {
    tmpData->tW = (int)(tmpData->w_curTemp->ra[0].size());
    tmpData->tH = (int)(tmpData->w_curTemp->ra.size());

    std::vector<std::vector<int>> copyRa(tmpData->w_curTemp->ra);
    tmpData->thumbArea = std::move(copyRa);
    
    std::vector<std::vector<int>> copyFinalCo(tmpData->tH * 3 + 1, std::vector<int>(tmpData->tW * 3 + 2, 1));
    tmpData->finalAreaData->co =std::move(copyFinalCo);
    
    std::vector<std::vector<int>> copyFinalTe(tmpData->tH * 3 + 1, std::vector<int>(tmpData->tW * 3 + 2, 1));
    tmpData->finalAreaData->te =std::move(copyFinalTe);
}

// 在地图上填数据 （无边缘检测）
static void fillArea(std::vector<std::vector<int>> &data, int beginX, int beginY, int edgeW, int edgeH, int key) {
    for (int i = 0; i < edgeW; i++) {
        int curX = beginX + i;
        for (int j = 0; j < edgeH; j++) {
            int curY = beginY + j;
            data[curY][curX] = key;
        }
    }
}

// 把地图空的地方填上数据 （带边缘检测）
static void fillBlankArea(std::vector<std::vector<int>> &data, int beginX, int beginY, int edgeW, int edgeH, int key) {
    int WMax = (int)data[0].size();
    int HMax = (int)data.size();
    for (int i = 0; i < edgeW; i++) {
        int curX = beginX + i;
        if (curX < 0) continue;
        if (curX >= WMax) break;
        for (int j = 0; j < edgeH; j++) {
            int curY = beginY + j;
            if (curY < 0) continue;
            if (curY >= HMax) break;
            if (data[curY][curX] == 0) {
                data[curY][curX] = key;
            }
        }
    }
}

void MapCreator::digHole(AreaTmpData* tmpData) {
    int mapTW = tmpData->tW;
    int mapTH = tmpData->tH;
    auto holeTArea = std::move(tmpData->thumbArea); // 右值引用，拉出来做处理，之后再放回去

    int mapTMax = mapTW * mapTH;
    float holeRatio = 0.3; //llytodo 要从js传入
    int holeBlockSize = (int)((float)mapTMax * holeRatio);

    int holeIndex = 0;

    // 处理固定块 并把固定块镶边
    int fiIndex = -1;
    for (FiTemp* fi : tmpData->w_curTemp->fis) {
        fiIndex++;
        fillArea(holeTArea, fi->tX, fi->tY, fi->tW, fi->tH, FI_HOLE_ID_BEGIN + holeIndex);
        fillBlankArea(holeTArea, fi->tX - 1, fi->tY - 1, fi->tW + 2, fi->tH + 2, FI_EDGE_ID_BEGIN + holeIndex); // 镶边

        auto holeData = new HoleData(fi->tX, fi->tY, fi->tW, fi->tH, HoleType::fi, holeIndex);
        holeData->fiIndex = fiIndex;
        if (fi->door[0].size() > 0) holeData->doorDir |= DOOR_UP;
        if (fi->door[1].size() > 0) holeData->doorDir |= DOOR_DOWN;
        if (fi->door[2].size() > 0) holeData->doorDir |= DOOR_LEFT;
        if (fi->door[3].size() > 0) holeData->doorDir |= DOOR_RIGHT;
        for (int i = 0; i < 4; i++) { holeData->substitutesMap[1 << i] = 1 << fi->substitutes[i]; }
        tmpData->holeVec.push_back(holeData);
        holeIndex++;
    }

    // 开始挖坑
    int creatingDir = -1; // 挖坑方向

    while (true) {
        // 反转挖坑方向，为了让效果更平均，所以从不同的方向挖倔
        creatingDir *= -1;

        // 随机获取一个位置
        int tx = getRandom(0, mapTW - 1);
        int ty = getRandom(0, mapTH - 1);
        int value = holeTArea[ty][tx];

        if (value != 0) { // 如果所取位置已经使用过，则获取另一个位置，但保证尽量在有限的随机次数内完成
            tx = mapTW - 1 - tx; // 从对称位置开始
            ty = mapTH - 1 - ty;
            bool needContinue = false;
            while (true) {
                tx += creatingDir;
                if (creatingDir > 0) {
                    if (tx >= mapTW) {
                        tx = 0;
                        ty += creatingDir;
                        if (ty >= mapTH) {
                            needContinue = true;
                            break;
                        }
                    }
                } else {
                    if (tx < 0) {
                        tx = mapTW - 1;
                        ty += creatingDir;
                        if (ty < 0) {
                            needContinue = true;
                            break;
                        }
                    }
                }
                value = holeTArea[ty][tx];
                if (value == 0) break;
            }

            if (needContinue) continue;
        }

        // 获得随机宽高最大值
        int holeTWMax = getRandom(3, MAX_R_TW);
        int holeTHMax = getRandom(3, MAX_R_TH);

        int curTX = tx;
        int curTY = ty;

        // 获取宽度
        int holeTW = 1;
        for (; holeTW < holeTWMax; holeTW++) {
            int subCurX = tx + holeTW * creatingDir;
            if (subCurX < 0 || holeTArea[ty].size() <= subCurX) break;

            int curValue = holeTArea[ty][subCurX];
            if (curValue == 0) curTX = subCurX;
            else break;
        }

        // 获取高度
        int holeTH = 1;
        for (; holeTH < holeTHMax; holeTH++) {
            int subCurY = ty + holeTH * creatingDir;
            if (subCurY < 0 || holeTArea.size() <= subCurY) break;

            int curValue = holeTArea[subCurY][tx];
            if (curValue == 0) curTY = subCurY;
            else break;
        }

        // 检测另一边是否有阻挡
        for (int holeW2 = 1; holeW2 < holeTW; holeW2++) {
            int curX2 = tx + holeW2 * creatingDir;
            int curValue = holeTArea[curTY][curX2];
            if (curValue == 0) curTX = curX2;
            else {
                holeTW = holeW2;
                break;
            }
        }

        for (int holeH2 = 1; holeH2 < holeTH; holeH2++) {
            int curY2 = ty + holeH2 * creatingDir;
            int curValue = holeTArea[curY2][curTX];
            if (curValue == 0) curTY = curY2;
            else {
                holeTH = holeH2;
                break;
            }
        }

        // 尽量范围不小于3
        if (holeTW < 3) {
            while (true) {
                if (creatingDir > 0 ? tx == 0 : tx == mapTW - 1) break;

                bool canExtand = true;
                for (int i = 0; i < holeTH; i++) {
                    if (holeTArea[ty + i * creatingDir][tx - creatingDir] != 0) {
                        canExtand = false;
                        break;
                    }
                }
                if (!canExtand) break;
                tx -= creatingDir;
                holeTW++;
                if (holeTW >= 3) break;
            }
        }

        if (holeTH < 3) {
            while (true) {
                if (creatingDir > 0 ? ty == 0 : ty == mapTH - 1) break;

                bool canExtand = true;
                for (int i = 0; i < holeTW; i++) {
                    if (holeTArea[ty - creatingDir][tx + i * creatingDir] != 0) {
                        canExtand = false;
                        break;
                    }
                }
                if (!canExtand) break;
                ty -= creatingDir;
                holeTH++;
                if (holeTH >= 3) break;
            }
        }

        // 记录新hole
        int beginX = creatingDir > 0 ? tx : curTX;
        int beginY = creatingDir > 0 ? ty : curTY;
        fillArea(holeTArea, beginX, beginY, holeTW, holeTH, RA_HOLE_ID_BEGIN + holeIndex);
        fillBlankArea(holeTArea, beginX - 1, beginY - 1, holeTW + 2, holeTH + 2, RA_EDGE_ID_BEGIN + holeIndex); // 镶边
        tmpData->holeVec.push_back(new HoleData(beginX, beginY, holeTW, holeTH, HoleType::ra, holeIndex));

        // 检测是否完成
        holeBlockSize -= (holeTW * holeTH);
        if (holeBlockSize <= 0) break;

        holeIndex++;
    }

    tmpData->thumbArea = std::move(holeTArea); // 处理后的数据放回原处
    printVecVec(tmpData->thumbArea);
}

static HoleDirOffsetType calcHoleDirOffsetType(float myPos, float myHalf, float anoPos, float anoHalf) {
    bool downOrRightOffset = myPos + myHalf < anoPos + anoHalf;
    bool upOrLeftOffset = myPos - myHalf > anoPos - anoHalf;
    if (downOrRightOffset && upOrLeftOffset) return HoleDirOffsetType::full;
    else if (downOrRightOffset) return HoleDirOffsetType::right_or_down;
    else if (upOrLeftOffset) return HoleDirOffsetType::left_or_up;
    else return HoleDirOffsetType::none;
}

#define GET_DIS_AND_OFFSET_TYPE_FOR_REMOVE(disName, typeName1, typeName2) \
disName = thisRelation->distance; \
switch (thisRelation->offsetType) { \
    case HoleDirOffsetType::full: typeName1 = true; typeName2 = true; break; \
    case HoleDirOffsetType::left_or_up: typeName1 = true; break; \
    case HoleDirOffsetType::right_or_down: typeName2 = true; break; \
    default: break; \
}

#define CHECK_DIS_AND_REMOVE(myDis, checkDis) \
if ((myDis) > (checkDis)) { \
    hole->relations.erase(hole->relations.begin() + i); i--; continue; \
} \

// 计算出hole之间的关系
void MapCreator::calcHoleRelation(AreaTmpData* tmpData) {
    int myIndex = -1;
    for (HoleData* hole : tmpData->holeVec) {
        myIndex++;

        float halfTW = (float)hole->tW / 2;
        float halfTH = (float)hole->tH / 2;
        float centerTX = (float)hole->tX + halfTW;
        float centerTY = (float)hole->tY + halfTH;

        int anoIndex = -1;
        for (HoleData* anotherHole : tmpData->holeVec) {
            anoIndex++;
            if (hole == anotherHole) continue;

            float anoHalfTW = (float)anotherHole->tW / 2;
            float anoHalfTH = (float)anotherHole->tH / 2;
            float anoCenterTX = (float)anotherHole->tX + anoHalfTW;
            float anoCenterTY = (float)anotherHole->tY + anoHalfTH;

            // 方向和距离
            HoleDir holeDir;
            HoleDirOffsetType offsetType = HoleDirOffsetType::none;
            float distance;
            if (centerTX + halfTW <= anoCenterTX - anoHalfTW) {
                float wDis = (anoCenterTX - anoHalfTW) - (centerTX + halfTW);
                if (centerTY + halfTH <= anoCenterTY - anoHalfTH) {
                    holeDir = HoleDir::rig_bot;
                    distance = (anoCenterTY - anoHalfTH) - (centerTY + halfTH) + wDis;
                } else if (centerTY - halfTH >= anoCenterTY + anoHalfTH) {
                    holeDir = HoleDir::rig_top;
                    distance = (centerTY - halfTH) - (anoCenterTY + anoHalfTH) + wDis;
                } else {
                    holeDir = HoleDir::rig_mid;
                    distance = wDis;
                    offsetType = calcHoleDirOffsetType(centerTY, halfTH, anoCenterTY, anoHalfTH);
                }
            } else if (centerTX - halfTW >= anoCenterTX + anoHalfTW) {
                float wDis = (centerTX - halfTW) - (anoCenterTX + anoHalfTW);
                if (centerTY + halfTH <= anoCenterTY - anoHalfTH) {
                    holeDir = HoleDir::lef_bot;
                    distance = (anoCenterTY - anoHalfTH) - (centerTY + halfTH) + wDis;
                } else if (centerTY - halfTH >= anoCenterTY + anoHalfTH) {
                    holeDir = HoleDir::lef_top;
                    distance = (centerTY - halfTH) - (anoCenterTY + anoHalfTH) + wDis;
                }else {
                    holeDir = HoleDir::lef_mid;
                    distance = wDis;
                    offsetType = calcHoleDirOffsetType(centerTY, halfTH, anoCenterTY, anoHalfTH);
                }
            } else {
                if (centerTY < anoCenterTY) {
                    holeDir = HoleDir::mid_bot;
                    distance = (anoCenterTY - anoHalfTH) - (centerTY + halfTH);
                    offsetType = calcHoleDirOffsetType(centerTX, halfTW, anoCenterTX, anoHalfTW);
                } else {
                    holeDir = HoleDir::mid_top;
                    distance = (centerTY - halfTH) - (anoCenterTY + anoHalfTH);
                    offsetType = calcHoleDirOffsetType(centerTX, halfTW, anoCenterTX, anoHalfTW);
                }
            }

            // 排序
            HoleRelation* relation = new HoleRelation();
            relation->dir = holeDir;
            relation->offsetType = offsetType;
            relation->distance = distance;
            relation->myHoleIndex = myIndex;
            relation->anoHoleIndex = anoIndex;

            bool needSort = true;
            for (int i = 0; i < hole->relations.size(); i++) { // 去掉重复dir的relation
                HoleRelation* anoRelation = hole->relations[i];
                if (relation->dir == anoRelation->dir) {
                    if (relation->distance >= anoRelation->distance) needSort = false;
                    else {
                        hole->relations.erase(hole->relations.begin() + i);
                        delete anoRelation;
                    }
                    break;
                }
            }

            if (needSort) {
                int relationIndex = 0;
                while (true) { // 按照dis从小到大排序
                    if (relationIndex == hole->relations.size()) {
                        hole->relations.push_back(relation);
                        break;
                    }

                    HoleRelation* anoRelation = hole->relations[relationIndex];
                    if (relation->distance < anoRelation->distance) {
                        HoleRelation* tmpRelation = relation;
                        relation = anoRelation;
                        hole->relations[relationIndex] = tmpRelation;
                    }

                    relationIndex++;
                }
            }
        }

        // 根据offset，移除太远的角的关系
        float leftDis, rightDis, upDis, downDis;
        bool leftUpOffset = false, leftDownOffset = false, rightUpOffset = false, rightDownOffset = false,
        topLeftOffset = false, topRightOffset = false, botLeftOffset = false, botRightOffset = false;
        for (int i = 0; i < hole->relations.size(); i++) {
            HoleRelation* thisRelation = hole->relations[i];
            HoleData* anoHole = tmpData->holeVec[thisRelation->anoHoleIndex];
            switch (thisRelation->dir) {
                case HoleDir::lef_mid:
                    GET_DIS_AND_OFFSET_TYPE_FOR_REMOVE(leftDis, leftUpOffset, leftDownOffset);
                    break;
                case HoleDir::rig_mid:
                    GET_DIS_AND_OFFSET_TYPE_FOR_REMOVE(rightDis, rightUpOffset, rightDownOffset);
                    break;
                case HoleDir::mid_top:
                    GET_DIS_AND_OFFSET_TYPE_FOR_REMOVE(upDis, topLeftOffset, topRightOffset);
                    break;
                case HoleDir::mid_bot:
                    GET_DIS_AND_OFFSET_TYPE_FOR_REMOVE(downDis, botLeftOffset, botRightOffset);
                    break;

                case HoleDir::lef_top:
                    if (leftUpOffset) {
                        CHECK_DIS_AND_REMOVE((centerTX - halfTW) - (anoHole->tX + anoHole->tW), leftDis);
                    }
                    if (topLeftOffset) {
                        CHECK_DIS_AND_REMOVE((centerTY - halfTH) - (anoHole->tY + anoHole->tH), upDis);
                    }
                    break;
                case HoleDir::lef_bot:
                    if (leftDownOffset) {
                        CHECK_DIS_AND_REMOVE((centerTX - halfTW) - (anoHole->tX + anoHole->tW), leftDis);
                    }
                    if (botLeftOffset) {
                        CHECK_DIS_AND_REMOVE((float)anoHole->tY - (centerTY + halfTH), downDis);
                    }
                    break;
                case HoleDir::rig_top:
                    if (rightUpOffset) {
                        CHECK_DIS_AND_REMOVE((float)anoHole->tX - (centerTX + halfTW), rightDis);
                    }
                    if (topRightOffset) {
                        CHECK_DIS_AND_REMOVE((centerTY - halfTH) - (anoHole->tY + anoHole->tH), upDis);
                    }
                    break;
                case HoleDir::rig_bot:
                    if (rightDownOffset) {
                        CHECK_DIS_AND_REMOVE((float)anoHole->tX - (centerTX + halfTW), rightDis);
                    }
                    if (botRightOffset) {
                        CHECK_DIS_AND_REMOVE((float)anoHole->tY - (centerTY + halfTH), downDis);
                    }
                    break;
            }
        }
    }
}

static HoleDir getOppositeHoleDir(HoleDir dir) {
    switch (dir) {
        case HoleDir::lef_mid: return HoleDir::rig_mid;
        case HoleDir::rig_mid: return HoleDir::lef_mid;
        case HoleDir::mid_top: return HoleDir::mid_bot;
        case HoleDir::mid_bot: return HoleDir::mid_top;
        case HoleDir::lef_top: return HoleDir::rig_bot;
        case HoleDir::lef_bot: return HoleDir::rig_top;
        case HoleDir::rig_top: return HoleDir::lef_bot;
        case HoleDir::rig_bot: return HoleDir::lef_top;
    }
}

// 把斜向的dir转成直向的
static HoleDir getStraightHoleDir(HoleDir dir) {
    switch (dir) {
        case HoleDir::lef_top: return (ifInPercent(50) ? HoleDir::lef_mid : HoleDir::mid_top);
        case HoleDir::lef_bot: return (ifInPercent(50) ? HoleDir::lef_mid : HoleDir::mid_bot);
        case HoleDir::rig_top: return (ifInPercent(50) ? HoleDir::rig_mid : HoleDir::mid_top);
        case HoleDir::rig_bot: return (ifInPercent(50) ? HoleDir::rig_mid : HoleDir::mid_bot);
        default: return dir;
    }
}

static int getDoorDirFromStraightHoleDir(HoleDir dir) {
    switch (dir) {
        case HoleDir::lef_mid: return DOOR_LEFT;
        case HoleDir::rig_mid: return DOOR_RIGHT;
        case HoleDir::mid_top: return DOOR_UP;
        case HoleDir::mid_bot: return DOOR_DOWN;
        default:
            throw "wrong StraightHoleDir";
            return 0; // 不会到这里
    }
}

static PipeEndPoint* createPipeEndPoint(HoleData* hole, HoleDir dir) {
    PipeEndPoint* endPoint = new PipeEndPoint();

    endPoint->holeIndex = hole->index;

    if (hole->type == HoleType::fi) { // 固定块如果有不能连接的方向，则用另一个方向替代
        int ddir = getDoorDirFromStraightHoleDir(getStraightHoleDir(dir));
        int substitutesDir = hole->substitutesMap.find(ddir)->second; // 必定存在
        endPoint->dir = substitutesDir;
    } else {
        HoleDir strgightDir = getStraightHoleDir(dir);
        endPoint->dir = getDoorDirFromStraightHoleDir(strgightDir);
        hole->doorDir |= endPoint->dir;
    }

    return endPoint;
}

static PipeData* createPipe(AreaTmpData* tmpData, HoleRelation* relation) {
    HoleData* my = tmpData->holeVec[relation->myHoleIndex];
    HoleData* another = tmpData->holeVec[relation->anoHoleIndex];
    HoleDir dir = relation->dir;
    HoleDir oppositeDir = getOppositeHoleDir(relation->dir);

    PipeData* pipe = new PipeData();
    pipe->endPoints[0] = createPipeEndPoint(my, dir);
    pipe->endPoints[1] = createPipeEndPoint(another, oppositeDir);
    return pipe;
}

static void dealRelationPathForConnection(AreaTmpData* tmpData, HoleRelation* relation, HoleData* anoHole);

static void dealEachRelationForConnection(AreaTmpData* tmpData, HoleData* hole) {
    for (HoleRelation* relation : hole->relations) {
        HoleData* anoHole = tmpData->holeVec[relation->anoHoleIndex];
        if (anoHole->inCircuit) continue;
        dealRelationPathForConnection(tmpData, relation, anoHole);
    }
}

static void dealRelationPathForConnection(AreaTmpData* tmpData, HoleRelation* relation, HoleData* anoHole) {
    anoHole->inCircuit = true;
    HoleRelation* curRelation = relation;
    HoleRelation* oppRelation = nullptr; // 同一个pipe但记录在对面hole中的关系

    // 查看有没有更近的已经在通路的坑
    int myIndex = relation->myHoleIndex;
    for (HoleRelation* anoRelation : anoHole->relations) {
        if (anoRelation->anoHoleIndex == myIndex) {
            oppRelation = anoRelation;
            break;
        }
        HoleData* anoAnoHole = tmpData->holeVec[anoRelation->anoHoleIndex];
        if (anoAnoHole->inCircuit) {
            curRelation = anoRelation;
            int anoIndex = anoHole->index;
            for (HoleRelation* anoAnoRelation : anoAnoHole->relations) {
                if (anoAnoRelation->anoHoleIndex == anoIndex) {
                    oppRelation = anoAnoRelation;
                    break;
                }
            }
            break;
        }
    }

    tmpData->pipeVec.push_back(createPipe(tmpData, curRelation));
    curRelation->pipeIndex = (int)tmpData->pipeVec.size() - 1;
    if (oppRelation) oppRelation->pipeIndex = curRelation->pipeIndex;

    dealEachRelationForConnection(tmpData, anoHole);
}

// 根据关系，连接所有的hole，形成通路
void MapCreator::connectAllHole(AreaTmpData* tmpData) {
    HoleData* hole = tmpData->holeVec[0];
    hole->inCircuit = true;
    dealEachRelationForConnection(tmpData, hole);
}

// 生成额外的通路，使表现更丰富，通路可通可不通
void MapCreator::connectExtraHole(AreaTmpData* tmpData) {
    std::vector<HoleRelation*> unusedRelations;
    std::set<int> unusedIndexKeySet;
    for (HoleData* hole : tmpData->holeVec) {
        for (HoleRelation* relation : hole->relations) {
            if (relation->pipeIndex == -1) {
                int oppIndexKey = relation->anoHoleIndex * 100 + relation->myHoleIndex;
                if (unusedIndexKeySet.find(oppIndexKey) != unusedIndexKeySet.end()) continue;

                unusedRelations.push_back(relation);
                int myIndexKey = relation->myHoleIndex * 100 + relation->anoHoleIndex;
                unusedIndexKeySet.insert(myIndexKey);
            }
        }
    }

    int extraCount = (int)unusedRelations.size() / getRandom(2, 3);
    for (int _ = 0; _ < extraCount; _++) {
        int index = getRandom(0, (int)unusedRelations.size() - 1);
        HoleRelation* curRelation = unusedRelations[index];
        PipeData* pipe = createPipe(tmpData, curRelation);
        pipe->connected = ifInPercent(50);
        tmpData->pipeVec.push_back(pipe);
        unusedRelations.erase(unusedRelations.begin() + index);
    }
}

static EleDoorType getEleDirTypesFromHoleDoorDir(int doorDir) {
    bool doorTypes[MAX_DOOR_TYPE] = { 0 }; // 反向获取，true为不可以的类型
    if ((doorDir & DOOR_UP) == DOOR_UP) {
        doorTypes[(int)EleDoorType::lef] = true;
        doorTypes[(int)EleDoorType::rig] = true;
        doorTypes[(int)EleDoorType::bot] = true;
        doorTypes[(int)EleDoorType::lef_bot] = true;
        doorTypes[(int)EleDoorType::rig_bot] = true;
        doorTypes[(int)EleDoorType::lef_rig] = true;
        doorTypes[(int)EleDoorType::rig_bot_lef] = true;
    }

    if ((doorDir & DOOR_DOWN) == DOOR_DOWN) {
        doorTypes[(int)EleDoorType::lef] = true;
        doorTypes[(int)EleDoorType::rig] = true;
        doorTypes[(int)EleDoorType::top] = true;
        doorTypes[(int)EleDoorType::lef_top] = true;
        doorTypes[(int)EleDoorType::rig_top] = true;
        doorTypes[(int)EleDoorType::lef_rig] = true;
        doorTypes[(int)EleDoorType::lef_top_rig] = true;
    }

    if ((doorDir & DOOR_LEFT) == DOOR_LEFT) {
        doorTypes[(int)EleDoorType::top] = true;
        doorTypes[(int)EleDoorType::rig] = true;
        doorTypes[(int)EleDoorType::bot] = true;
        doorTypes[(int)EleDoorType::rig_top] = true;
        doorTypes[(int)EleDoorType::rig_bot] = true;
        doorTypes[(int)EleDoorType::top_bot] = true;
        doorTypes[(int)EleDoorType::top_rig_bot] = true;
    }

    if ((doorDir & DOOR_RIGHT) == DOOR_RIGHT) {
        doorTypes[(int)EleDoorType::top] = true;
        doorTypes[(int)EleDoorType::lef] = true;
        doorTypes[(int)EleDoorType::bot] = true;
        doorTypes[(int)EleDoorType::lef_top] = true;
        doorTypes[(int)EleDoorType::lef_bot] = true;
        doorTypes[(int)EleDoorType::top_bot] = true;
        doorTypes[(int)EleDoorType::bot_lef_top] = true;
    }

    for (int i = 0; i < MAX_DOOR_TYPE; i++) {
        if (doorTypes[i] == false) return (EleDoorType)i;
    }
    return EleDoorType::all;
}

void MapCreator::assignEleToHole(AreaTmpData* tmpData) {
    int sceneKey = tmpData->curSceneKey == 10 ? 0 : 1;

    for (HoleData* hole : tmpData->holeVec) {
        if (hole->type == HoleType::fi) continue;
        EleDoorType eleDoorType = getEleDirTypesFromHoleDoorDir(hole->doorDir);

        std::vector<int> eleList = _mapEleList.list[hole->tW - 1][hole->tH - 1][(int)eleDoorType][sceneKey];
        int index = getRandom(0, (int)eleList.size() - 1);
        int eleIndex = eleList[index];
        hole->ele = this->_mapEleVec[eleIndex];
    }
}

static int getDoorDirIndexFromStraightDoorDir(int dir) {
    switch (dir) {
        case DOOR_UP: return 0;
        case DOOR_DOWN: return 1;
        case DOOR_LEFT: return 2;
        case DOOR_RIGHT: return 3;
        default:
            throw "wrong StraightDoorDir";
            return -1; // 不会到这里
    }
}

static void getEndPointPosition(AreaTmpData* tmpData, PipeEndPoint* endPoint, int* tX, int* tY) {
    HoleData* hole = tmpData->holeVec[endPoint->holeIndex];

    int doorDirIndex = getDoorDirIndexFromStraightDoorDir(endPoint->dir);
    std::vector<int>* doorPosList;
    if (hole->type == HoleType::fi) {
        doorPosList = &tmpData->w_curTemp->fis[hole->fiIndex]->door[doorDirIndex];
    } else {
        doorPosList = &hole->ele->door[doorDirIndex];
    }

    // 随机选择一个门的位置
    int doorIndex = getRandom(0, (int)doorPosList->size() - 1);
    int doorPos = (*doorPosList)[doorIndex];

    // 根据pipe终端的方向不同（也就是对应hole的方向），已经hole的位置，确定终端的方向
    switch (endPoint->dir) {
        case DOOR_UP:
            *tX = hole->tX + doorPos;
            *tY = hole->tY - 1;
            break;
        case DOOR_DOWN:
            *tX = hole->tX + doorPos;
            *tY = hole->tY + hole->tH;
            break;
        case DOOR_LEFT:
            *tX = hole->tX - 1;
            *tY = hole->tY + doorPos;
            break;
        case DOOR_RIGHT:
            *tX = hole->tX + hole->tW;
            *tY = hole->tY + doorPos;
            break;
        default:
            throw "wrong PipeEndPoint dir";
            break;
    }

    endPoint->tX = *tX;
    endPoint->tY = *tY;
}

void MapCreator::digPipe(AreaTmpData* tmpData) {
    std::vector<std::vector<int>> thumbArea = std::move(tmpData->thumbArea); // 右值引用，拉出来使用
    int thumbAreaWidth = (int)thumbArea[0].size();
    int thumbAreaHeight = (int)thumbArea.size();

    // 遍历所有管道，根据其两端门的位置，产生管道坐标
    int pipeIndex = -1;
    for(PipeData* pipe: tmpData->pipeVec) {
        pipeIndex++;

        PipeEndPoint* endPoint0 = pipe->endPoints[0];
        PipeEndPoint* endPoint1 = pipe->endPoints[1];

        int tX0, tY0, tX1, tY1, curX, curY, finalX, finalY;
        getEndPointPosition(tmpData, endPoint0, &tX0, &tY0);
        getEndPointPosition(tmpData, endPoint1, &tX1, &tY1);

        // 一定是y轴靠上的在前面，便于后面计算
        if (tY0 <= tY1) {
            curX = tX0; curY = tY0; finalX = tX1; finalY = tY1;
        } else {
            curX = tX1; curY = tY1; finalX = tX0; finalY = tY0;
            pipe->endPoints[0] = endPoint1;
            pipe->endPoints[1] = endPoint0;
        }

        // 根据thumb地图，从一个终端连到另一个终端
        int curPosIndex = -1;
        while (true) {
            curPosIndex++;

            // 记录pipe的点位
            int curThumb = thumbArea[curY][curX];
            if (curThumb < PIPE_ID_BEGIN) {
                int idBegin = curPosIndex == 0 ? PIPE_FIRST_BLOCK_ID : PIPE_ID_BEGIN;
                thumbArea[curY][curX] = idBegin + pipeIndex;
            }

            pipe->tXs.push_back(curX);
            pipe->tYs.push_back(curY);

            // 连接到了另一个终端
            if (curX == finalX && curY == finalY) {
                break;
            }

            int xDir = curX < finalX ? 1 : (curX == finalX ? 0 : -1);
            int yDir = curY < finalY ? 1 : (curY == finalY ? 0 : -1);
            bool pDirIsX;

            // 选择一个方向进行移动
            if (xDir == 0) {
                curY += yDir;
                pDirIsX = false;
            } else if (yDir == 0) {
                curX += xDir;
                pDirIsX = true;
            } else {
                int xMove, yMove, anoXMove, anoYMove;
                if (ifInPercent(50)) {
                    xMove = xDir; yMove = 0; anoXMove = 0; anoYMove = yDir; pDirIsX = true;
                } else {
                    xMove = 0; yMove = yDir; anoXMove = xDir; anoYMove = 0; pDirIsX = false;
                }

                int nextX = curX + xMove;
                int nextY = curY + yMove;

                // 如果移动的一边是hole，则用另一边
                if (nextX < 0 || thumbAreaWidth <= nextX || nextY < 0 || thumbAreaHeight <= nextY) {
                    nextX = curX + anoXMove;
                    nextY = curY + anoYMove;
                    pDirIsX = !pDirIsX;
                } else {
                    int nextThumb = thumbArea[nextY][nextX];
                    if (nextThumb >= FI_HOLE_ID_BEGIN && nextThumb < PIPE_ID_BEGIN) {
                        nextX = curX + anoXMove;
                        nextY = curY + anoYMove;
                        pDirIsX = !pDirIsX;
                    }
                }
                curX = nextX; curY = nextY;
            }
        }
    }

    tmpData->thumbArea = std::move(thumbArea); // 处理后的数据放回原处
    printVecVec(tmpData->thumbArea);
}

void MapCreator::calcSpines(AreaTmpData* tmpData) {
    for (HoleData* holeData : tmpData->holeVec) {
        if (holeData->type == HoleType::fi) continue;
        
        int pxBegin = (holeData->tX * 3 + 1) * TileLength;
        int pyBegin = (holeData->tY * 3) * TileLength;
        for (SpineData* spineData : holeData->ele->spineList) {
            SpineData* newData = new SpineData();
            newData->id = spineData->id;
            newData->pX = spineData->pX + pxBegin;
            newData->pY = spineData->pY + pyBegin;
            tmpData->finalAreaData->spineList.push_back(newData);
        }
    }
    
    for (SpineData* spineData : tmpData->w_curTemp->spineList) {
        tmpData->finalAreaData->spineList.push_back(spineData);
    }
}

static void createFinalMapForFi(AreaTmpData* tmpData) {
    for (FiTemp* fi : tmpData->w_curTemp->fis) {
        int beginFX = fi->rX;
        int beginFY = fi->rY;
        
        for (int x = 0; x < fi->rW; x++) {
            for (int y = 0; y < fi->rH; y++) {
                int coData = fi->co[y][x];
                tmpData->finalAreaData->co[beginFY + y][beginFX + x] = coData;
                
                int teData = fi->te[y][x];
                tmpData->finalAreaData->te[beginFY + y][beginFX + x] = teData;
            }
        }
    }
}

static std::vector<int> getEveryDigit(const int num, const int size) {
    std::vector<int> list(size, 0);
    int checkNum = num;
    for (int i = size - 1; i >= 0; i--) {
        list[i] = checkNum % 10;
        checkNum /= 10;
    }
    
    return list;
}

static void createFinalMapForHole(AreaTmpData* tmpData, const std::vector<MapEleBase*> &mapEleBaseVec) {
    for (HoleData* hole : tmpData->holeVec) {
        if (hole->type == HoleType::fi) continue;
        
        int beginFX = hole->tX * 3 + 1;
        int beginFY = hole->tY * 3;
        int curFX = beginFX;
        int curFY = beginFY;
        
        MapEle* ele = hole->ele;
        MapEleBase* base = mapEleBaseVec[ele->baseIndex];
        
        std::vector<int> hList = getEveryDigit(hole->ele->usingTYs, base->tH);
        std::vector<int> wList = getEveryDigit(hole->ele->usingTXs, base->tW);
        
        for (int hIndex = 0; hIndex < hList.size(); hIndex++) {
            int hKey = hList[hIndex];
            if (hKey == 0) continue;
            
            for (int subHIndex = 0; subHIndex < 3; subHIndex++) {
                int realY = hIndex * 3 + subHIndex;
                
                for (int wIndex = 0; wIndex < wList.size(); wIndex++) {
                    int wKey = wList[wIndex];
                    if (wKey == 0) continue;
                    
                    for (int subWIndex = 0; subWIndex < 3; subWIndex++) {
                        int realX = wIndex * 3 + subWIndex;
                        
                        int coData = base->co[realY][realX];
                        tmpData->finalAreaData->co[curFY][curFX] = coData;
                        tmpData->finalAreaData->te[curFY][curFX] = coData; // 这里的co和te是一样的，所以都用co
                        curFX++;
                    }
                }
                curFY++;
                curFX = beginFX;
            }
        }
    }
}

// 返回是否使用实地而不是平台
static bool fillFinalPipeBlockByPlatList(std::vector<int> platList, int beginX, int beginY, int pipeIndex, FinalAreaData* finalAreaData) {
    int blockUsing = false;
    
    for (int subHIndex = 0; subHIndex < 3; subHIndex++) {
        int realY = beginY + subHIndex;
        std::vector<int> mapDataList = {MAP_CO_DATA_BLANK, MAP_CO_DATA_BLANK, MAP_CO_DATA_BLANK};
        
        if (std::find(platList.begin(), platList.end(), subHIndex) != platList.end()) {
            if (!blockUsing && ifInPercent(35)) {
                mapDataList[ifInPercent(50) ? 0 : 2] = MAP_CO_DATA_BLOCK; // block 在中间怕不好跳
                blockUsing = true;
            } else {
                mapDataList[getRandom(0, 2)] = MAP_CO_DATA_PLAT;
            }
        }
        
        for (int subWIndex = 0; subWIndex < 3; subWIndex++) {
            int realX = beginX + subWIndex;
            int data = mapDataList[subWIndex];
            finalAreaData->co[realY][realX] = data;
            finalAreaData->te[realY][realX] = data;
        }
    }
    
    return blockUsing;
}

static void fillFinalPipeBlockByType(PipeBlockType type, int tX, int tY, int pipeIndex, AreaTmpData* tmpData) {
    int beginX = tX * 3 + 1;
    int beginY = tY * 3;
    FinalAreaData* finalAreaData = tmpData->finalAreaData;
    
    int curTData;
    bool blockUsing;
    switch (type) {
        case PipeBlockType::blank:
            fillFinalPipeBlockByPlatList({}, beginX, beginY, pipeIndex, finalAreaData);
            curTData = PIPE_TYPE_BLANK;
            break;
        case PipeBlockType::plat0:
            blockUsing = fillFinalPipeBlockByPlatList({0}, beginX, beginY, pipeIndex, finalAreaData);
            curTData = blockUsing ? PIPE_TYPE_0_BLOCK : PIPE_TYPE_0;
            break;
        case PipeBlockType::plat1:
            blockUsing = fillFinalPipeBlockByPlatList({1}, beginX, beginY, pipeIndex, finalAreaData);
            curTData = blockUsing ? PIPE_TYPE_1_BLOCK : PIPE_TYPE_1;
            break;
        case PipeBlockType::plat2:
            blockUsing = fillFinalPipeBlockByPlatList({2}, beginX, beginY, pipeIndex, finalAreaData);
            curTData = blockUsing ? PIPE_TYPE_2_BLOCK : PIPE_TYPE_2;
            break;
        case PipeBlockType::plat02:
            blockUsing = fillFinalPipeBlockByPlatList({0, 2}, beginX, beginY, pipeIndex, finalAreaData);
            curTData = blockUsing ? PIPE_TYPE_2_BLOCK : PIPE_TYPE_2;
            break;
        default:
            throw "wrong createFinalPipeBlockByType type";
            break;
    }

    tmpData->thumbArea[tY][tX] = curTData + pipeIndex;
}

static void fillFinalPipeBlockWithHoleAbove(int tX, int tY, int pipeIndex, AreaTmpData* tmpData, bool firstBlock) {
    int beginX = tX * 3 + 1;
    int beginY = tY * 3;
    FinalAreaData* finalAreaData = tmpData->finalAreaData;
    int thumbAreaType = PIPE_TYPE_0;

    for (int subHIndex = 0; subHIndex < 3; subHIndex++) {
        int realY = beginY + subHIndex;
        std::vector<int> mapDataList = {MAP_CO_DATA_BLANK, MAP_CO_DATA_BLANK, MAP_CO_DATA_BLANK};
        
        if (subHIndex == 0) {
            if (firstBlock) {
                if (finalAreaData->co[realY - 1][beginX] == MAP_CO_DATA_BLOCK) {
                    mapDataList[2] = MAP_CO_DATA_PLAT;
                    mapDataList[0] = MAP_CO_DATA_BLOCK;
                } else if (finalAreaData->co[realY - 1][beginX + 2] == MAP_CO_DATA_BLOCK) {
                    mapDataList[0] = MAP_CO_DATA_PLAT;
                    mapDataList[2] = MAP_CO_DATA_BLOCK;
                } else if (
                    finalAreaData->co[realY - 1][beginX]     == MAP_CO_DATA_PLAT_BG ||
                    finalAreaData->co[realY - 1][beginX + 1] == MAP_CO_DATA_PLAT_BG ||
                    finalAreaData->co[realY - 1][beginX + 2] == MAP_CO_DATA_PLAT_BG ||
                    finalAreaData->co[realY - 1][beginX]     == MAP_CO_DATA_PLAT_HEAD ||
                    finalAreaData->co[realY - 1][beginX + 1] == MAP_CO_DATA_PLAT_HEAD ||
                    finalAreaData->co[realY - 1][beginX + 2] == MAP_CO_DATA_PLAT_HEAD) {
                    mapDataList[0] = MAP_CO_DATA_PLAT;
                    mapDataList[1] = MAP_CO_DATA_PLAT;
                    mapDataList[2] = MAP_CO_DATA_PLAT;
                } else {
                    if (ifInPercent(50)) {
                        mapDataList[getRandom(0, 2)] = MAP_CO_DATA_PLAT;
                    } else {
                        thumbAreaType = PIPE_TYPE_1; // 第0行未处理，使用第1行
                    }
                }
            } else {
                if (finalAreaData->co[realY - 1][beginX] != MAP_CO_DATA_BLOCK) mapDataList[0] = MAP_CO_DATA_PLAT;
                if (finalAreaData->co[realY - 1][beginX + 1] != MAP_CO_DATA_BLOCK) mapDataList[1] = MAP_CO_DATA_PLAT;
                if (finalAreaData->co[realY - 1][beginX + 2] != MAP_CO_DATA_BLOCK) mapDataList[2] = MAP_CO_DATA_PLAT;
            }
        } else if (subHIndex == 1 && thumbAreaType == PIPE_TYPE_1) {
            mapDataList[getRandom(0, 2)] = MAP_CO_DATA_PLAT;
        }
        
        for (int subWIndex = 0; subWIndex < 3; subWIndex++) {
            int realX = beginX + subWIndex;
            int data = mapDataList[subWIndex];
            finalAreaData->co[realY][realX] = data;
            finalAreaData->te[realY][realX] = data;
        }
    }

    tmpData->thumbArea[tY][tX] = thumbAreaType + pipeIndex;
}

static void createFinalMapForPipe(AreaTmpData* tmpData) {
    for (int tY = 0; tY < tmpData->thumbArea.size(); tY++) {
        std::vector<int> tXList = tmpData->thumbArea[tY];
        for (int tX = 0; tX < tXList.size(); tX++) {
            int tData = tXList[tX];
            int pipeIndex = tData % PIPE_ID_BEGIN;
            
            if (tData < PIPE_ID_BEGIN) continue; // 过滤出管道
            
            if (tY == 0) {
                fillFinalPipeBlockByType(PipeBlockType::blank, tX, tY, pipeIndex, tmpData);
                
            } else {
                bool firstPipeBlock = (tData - pipeIndex == PIPE_FIRST_BLOCK_ID);
                
                int tDataAbove = tmpData->thumbArea[tY - 1][tX];
                
                bool connectedAbove = true; // 上边非连接，本块不用加跳台
                if (tDataAbove >= PIPE_ID_BEGIN) {
                    connectedAbove = tmpData->pipeVec[tDataAbove % PIPE_ID_BEGIN]->connected;
                }
                
                if (!connectedAbove) {
                    fillFinalPipeBlockByType(PipeBlockType::blank, tX, tY, pipeIndex, tmpData);
                    
                } else if (tDataAbove < FI_HOLE_ID_BEGIN) { // 实地
                    fillFinalPipeBlockByType(PipeBlockType::blank, tX, tY, pipeIndex, tmpData);
                    
                } else if (tDataAbove < PIPE_ID_BEGIN) { // hole
                    fillFinalPipeBlockWithHoleAbove(tX, tY, pipeIndex, tmpData, firstPipeBlock);
                    
                } else if (tDataAbove == PIPE_TYPE_0) {
                    PipeBlockType t = ifInPercent(66) ? PipeBlockType::plat0 : PipeBlockType::plat02;
                    fillFinalPipeBlockByType(t, tX, tY, pipeIndex, tmpData);
                    
                } else if (tDataAbove == PIPE_TYPE_1) {
                    PipeBlockType t = ifInPercent(66) ? PipeBlockType::plat1 : PipeBlockType::plat0;
                    fillFinalPipeBlockByType(t, tX, tY, pipeIndex, tmpData);
                    
                } else if (tDataAbove == PIPE_TYPE_2) {
                    PipeBlockType t = ifInPercent(66) ? PipeBlockType::plat2 : PipeBlockType::plat1;
                    fillFinalPipeBlockByType(t, tX, tY, pipeIndex, tmpData);
                    
                } else if (tDataAbove == PIPE_TYPE_0_BLOCK) {
                    PipeBlockType t = ifInPercent(66) ? PipeBlockType::plat0 : PipeBlockType::plat02;
                    fillFinalPipeBlockByType(t, tX, tY, pipeIndex, tmpData);
                    
                } else if (tDataAbove == PIPE_TYPE_1_BLOCK) {
                    fillFinalPipeBlockByType(PipeBlockType::plat1, tX, tY, pipeIndex, tmpData);
                    
                } else if (tDataAbove == PIPE_TYPE_2_BLOCK) {
                    fillFinalPipeBlockByType(PipeBlockType::plat2, tX, tY, pipeIndex, tmpData);
                    
                } else { // tDataAbove == PIPE_TYPE_BLANK 空的话，还要看更上一层
                    if (tY == 1 || tmpData->thumbArea[tY - 2][tX] < FI_HOLE_ID_BEGIN) {
                        PipeBlockType t = ifInPercent(66) ? PipeBlockType::blank : PipeBlockType::plat0;
                        fillFinalPipeBlockByType(t, tX, tY, pipeIndex, tmpData);
                    } else {
                        PipeBlockType t = ifInPercent(66) ? PipeBlockType::plat0 : PipeBlockType::plat02;
                        fillFinalPipeBlockByType(t, tX, tY, pipeIndex, tmpData);
                    }
                }
            }
        }
    }
}

static void setFinalAreaDataBlank(AreaTmpData* tmpData, int x, int y) {
    tmpData->finalAreaData->co[y][x] = MAP_CO_DATA_BLANK;
    tmpData->finalAreaData->te[y][x] = MAP_CO_DATA_BLANK;
}

// 给管道拓宽
static void createFinalMapForWidePipe(AreaTmpData* tmpData) {
    for (int tY = 1; tY < tmpData->thumbArea.size(); tY++) { // 舍去第一行
        std::vector<int> tXList = tmpData->thumbArea[tY];
        int beginY = tY * 3;
        for (int tX = 1; tX < tXList.size() - 1; tX++) { // 舍去左右的列
            int tData = tXList[tX];
            
            if (tData != 0) continue;
            int beginX = tX * 3 + 1;
            
            int wallAbove = tmpData->thumbArea[tY - 1][tX];
            
            int leftData = tmpData->thumbArea[tY][tX - 1];
            bool leftIsPipe = leftData > PIPE_ID_BEGIN && leftData < USING_WALL_LEFT;
            
            int rightData = tmpData->thumbArea[tY][tX + 1];
            bool rightIsPipe = rightData > PIPE_ID_BEGIN && rightData < USING_WALL_LEFT;
            int rKey = getRandom(1, 10);

            if (wallAbove == USING_WALL_LEFT) {
                if (leftIsPipe) {
                    if (rKey <= 6) {
                        tmpData->thumbArea[tY][tX] = USING_WALL_LEFT;
                        setFinalAreaDataBlank(tmpData, beginX, beginY);
                        setFinalAreaDataBlank(tmpData, beginX, beginY + 1);
                        setFinalAreaDataBlank(tmpData, beginX, beginY + 2);
                    } else if (rKey <= 8) {
                        setFinalAreaDataBlank(tmpData, beginX, beginY);
                        setFinalAreaDataBlank(tmpData, beginX, beginY + 1);
                    } else {
                        setFinalAreaDataBlank(tmpData, beginX, beginY);
                    }
                }
            } else if (wallAbove == USING_WALL_RIGHT) {
                if (rightIsPipe) {
                    if (rKey <= 6) {
                        tmpData->thumbArea[tY][tX] = USING_WALL_RIGHT;
                        setFinalAreaDataBlank(tmpData, beginX + 2, beginY);
                        setFinalAreaDataBlank(tmpData, beginX + 2, beginY + 1);
                        setFinalAreaDataBlank(tmpData, beginX + 2, beginY + 2);
                    } else if (rKey <= 8) {
                        setFinalAreaDataBlank(tmpData, beginX + 2, beginY);
                        setFinalAreaDataBlank(tmpData, beginX + 2, beginY + 1);
                    } else {
                        setFinalAreaDataBlank(tmpData, beginX + 2, beginY);
                    }
                }
            } else if (PIPE_ID_BEGIN <= wallAbove && wallAbove < USING_WALL_LEFT) { // 上面是管道
                if (leftIsPipe) {
                    if (rKey <= 3) {
                        tmpData->thumbArea[tY][tX] = USING_WALL_LEFT;
                        setFinalAreaDataBlank(tmpData, beginX, beginY);
                        setFinalAreaDataBlank(tmpData, beginX, beginY + 1);
                        setFinalAreaDataBlank(tmpData, beginX, beginY + 2);
                    } else if (rKey <= 6) {
                        setFinalAreaDataBlank(tmpData, beginX, beginY);
                    }
                } else if (rightIsPipe) {
                    if (rKey <= 3) {
                        tmpData->thumbArea[tY][tX] = USING_WALL_RIGHT;
                        setFinalAreaDataBlank(tmpData, beginX + 2, beginY);
                        setFinalAreaDataBlank(tmpData, beginX + 2, beginY + 1);
                        setFinalAreaDataBlank(tmpData, beginX + 2, beginY + 2);
                    } else if (rKey <= 6) {
                        setFinalAreaDataBlank(tmpData, beginX + 2, beginY);
                    }
                }
            } else {
                if (leftIsPipe) {
                    if (rKey <= 5) {
                        tmpData->thumbArea[tY][tX] = USING_WALL_LEFT;
                        setFinalAreaDataBlank(tmpData, beginX, beginY + 2);
                    }
                } else if (rightIsPipe) {
                    if (rKey <= 5) {
                        tmpData->thumbArea[tY][tX] = USING_WALL_RIGHT;
                        setFinalAreaDataBlank(tmpData, beginX + 2, beginY + 2);
                    }
                }
            }
        }
    }
}

static void finishHoleFirstLine(AreaTmpData* tmpData) {
    for (HoleData* hole : tmpData->holeVec) {
        if (hole->tY == 0) continue;
        
        bool aboveDigging = ifInPercent(50); // 是否挖掉更上的一行
        
        int beginX = hole->tX * 3 + 1;
        int beginY = hole->tY * 3;
        int yAbove = beginY - 1;
        int height = hole->tW * 3;

        std::vector<std::vector<int>> platXVecVec;
        platXVecVec.resize(hole->tW);
        bool blank = false; // 从非空变成空，重新计入一个新的vec中，让连续的空放在一起，以便去除两边
        int platXVecIndex = -1;

        for (int x = 0; x < height; x++) {
            int realX = beginX + x;
            int data = tmpData->finalAreaData->co[beginY][realX];
            if (data != MAP_CO_DATA_BLANK) continue;

            int dataAbove = tmpData->finalAreaData->co[yAbove][realX];
            
            if (dataAbove == MAP_CO_DATA_BLANK) {
                if (blank == false) {
                    blank = true;
                    platXVecIndex++;
                }
                platXVecVec[platXVecIndex].push_back(realX);
            } else {
                blank = false;
                
                if (aboveDigging) {
                    int realTX = (realX - 1) / 3;
                    int thumbDataAbove = tmpData->thumbArea[hole->tY - 1][realTX];
                    if (thumbDataAbove < FI_HOLE_ID_BEGIN || thumbDataAbove >= USING_WALL_LEFT) {
                        tmpData->finalAreaData->co[yAbove][realX] = MAP_CO_DATA_BLANK;
                        tmpData->finalAreaData->te[yAbove][realX] = MAP_CO_DATA_BLANK;
                    }
                }
            }
        }
        
        for (auto platXVec : platXVecVec) {
            // 掐头去尾
            if (platXVec.size() > 2) {
                platXVec[0] = -1;
                platXVec[platXVec.size() - 1] = -1;
            }
            
            for (int x : platXVec) {
                if (x < 0) continue;
                tmpData->finalAreaData->co[beginY][x] = MAP_CO_DATA_PLAT;
                tmpData->finalAreaData->te[beginY][x] = MAP_CO_DATA_PLAT;
            }
        }
    }
}

static void finishMapFirstLine(AreaTmpData* tmpData) {
    std::vector<int> tXList = tmpData->thumbArea[0];
    for (int tX = 0; tX < tXList.size(); tX++) {
        int tData = tXList[tX];
        if (tData >= FI_HOLE_ID_BEGIN && tData < RA_HOLE_ID_BEGIN) continue;
        
        int beginX = tX * 3 + 1;
        for (int subWIndex = 0; subWIndex < 3; subWIndex++) {
            int realX = beginX + subWIndex;
            tmpData->finalAreaData->co[0][realX] = MAP_CO_DATA_BLOCK;
            tmpData->finalAreaData->te[0][realX] = MAP_CO_DATA_BLOCK;
        }
    }
}

void MapCreator::createFinalArea(AreaTmpData* tmpData) {
    createFinalMapForFi(tmpData);
    createFinalMapForHole(tmpData, _mapEleBaseVec);
    
    createFinalMapForPipe(tmpData);
    createFinalMapForWidePipe(tmpData);
    
    finishHoleFirstLine(tmpData);
    finishMapFirstLine(tmpData);
    
//    printVecVecToFile(tmpData->finalAreaData->co, "myMap/mapCo.csv");
//    printVecVecToFile(tmpData->finalAreaData->te, "myMap/mapTe.csv");
}

// 完善平台的背景
static void finishPlatBG(AreaTmpData* tmpData) {
    std::vector<std::vector<int>>* pTe = &(tmpData->finalAreaData->te);
    
    for (int ry = 0; ry < pTe->size(); ry++) {
        std::vector<int>* pTeLine = &(*pTe)[ry];
        bool atPlat = false;
        for (int rx = 0; rx < pTeLine->size(); rx++) {
            int teData = (*pTeLine)[rx];
            
            // 因为tilemap里面只放head和bg就好，不能放左右，而且head一定在bg上面，所以只判断head就好
            if (teData != MAP_CO_DATA_PLAT_HEAD) continue;
            
            int subData = -1;
            if (!atPlat) {
                atPlat = true;
                (*pTe)[ry][rx] = MAP_CO_DATA_PLAT_HEAD_L;
                subData = MAP_CO_DATA_PLAT_BG_L;

            } else if ((*pTeLine)[rx + 1] != MAP_CO_DATA_PLAT_HEAD) { // +1不用判断出界，因为不可能
                atPlat = false;
                (*pTe)[ry][rx] = MAP_CO_DATA_PLAT_HEAD_R;
                subData = MAP_CO_DATA_PLAT_BG_R;
            }
            
            if (subData != -1) {
                int ky = ry;
                while (true) {
                    ky++;
                    int kData = (*pTe)[ky][rx];
                    if (kData == MAP_CO_DATA_PLAT_BG) {
                        (*pTe)[ky][rx] = subData;
                    } else {
                        break;
                    }
                }
            }
        }
    }
}

static std::map <int, int> teDirTypeMap = {
    // 四周全空
    {0b00000000,  2}, {0b00000001,  2}, {0b00000010,  2}, {0b00000011,  2}, {0b00000100,  2}, {0b00000101,  2}, {0b00000110,  2}, {0b00000111,  2},
    {0b00001000,  2}, {0b00001001,  2}, {0b00001010,  2}, {0b00001011,  2}, {0b00001100,  2}, {0b00001101,  2}, {0b00001110,  2}, {0b00001111,  2},
    
    // 只有一边
    {0b10000000, 16}, {0b10000001, 16}, {0b10000010, 16}, {0b10000011, 16}, {0b10000100, 16}, {0b10000101, 16}, {0b10000110, 16}, {0b10000111, 16},
    {0b10001000, 16}, {0b10001001, 16}, {0b10001010, 16}, {0b10001011, 16}, {0b10001100, 16}, {0b10001101, 16}, {0b10001110, 16}, {0b10001111, 16},
    
    {0b01000000, 16}, {0b01000001, 16}, {0b01000010, 16}, {0b01000011, 16}, {0b01000100, 16}, {0b01000101, 16}, {0b01000110, 16}, {0b01000111, 16},
    {0b01001000, 16}, {0b01001001, 16}, {0b01001010, 16}, {0b01001011, 16}, {0b01001100, 16}, {0b01001101, 16}, {0b01001110, 16}, {0b01001111, 16},
    
    {0b00100000, 15}, {0b00100001, 15}, {0b00100010, 15}, {0b00100011, 15}, {0b00100100, 15}, {0b00100101, 15}, {0b00100110, 15}, {0b00100111, 15},
    {0b00101000, 15}, {0b00101001, 15}, {0b00101010, 15}, {0b00101011, 15}, {0b00101100, 15}, {0b00101101, 15}, {0b00101110, 15}, {0b00101111, 15},
    
    {0b00010000, 15}, {0b00010001, 15}, {0b00010010, 15}, {0b00010011, 15}, {0b00010100, 15}, {0b00010101, 15}, {0b00010110, 15}, {0b00010111, 15},
    {0b00011000, 15}, {0b00011001, 15}, {0b00011010, 15}, {0b00011011, 15}, {0b00011100, 15}, {0b00011101, 15}, {0b00011110, 15}, {0b00011111, 15},
    
    // 左右 上下
    {0b11000000, 16}, {0b11000001, 16}, {0b11000010, 16}, {0b11000011, 16}, {0b11000100, 16}, {0b11000101, 16}, {0b11000110, 16}, {0b11000111, 16},
    {0b11001000, 16}, {0b11001001, 16}, {0b11001010, 16}, {0b11001011, 16}, {0b11001100, 16}, {0b11001101, 16}, {0b11001110, 16}, {0b11001111, 16},
    
    {0b00110000, 15}, {0b00110001, 15}, {0b00110010, 15}, {0b00110011, 15}, {0b00110100, 15}, {0b00110101, 15}, {0b00110110, 15}, {0b00110111, 15},
    {0b00111000, 15}, {0b00111001, 15}, {0b00111010, 15}, {0b00111011, 15}, {0b00111100, 15}, {0b00111101, 15}, {0b00111110, 15}, {0b00111111, 15},
    
    // 两方向
    {0b10100000,  2}, {0b10100001,  2}, {0b10100010,  2}, {0b10100011,  2}, {0b10100100,  2}, {0b10100101,  2}, {0b10100110,  2}, {0b10100111,  2},
    {0b10101000, 14}, {0b10101001, 14}, {0b10101010, 14}, {0b10101011, 14}, {0b10101100, 14}, {0b10101101, 14}, {0b10101110, 14}, {0b10101111, 14},
    
    {0b01100000,  2}, {0b01100001,  2}, {0b01100010,  2}, {0b01100011,  2}, {0b01100100, 13}, {0b01100101, 13}, {0b01100110, 13}, {0b01100111, 13},
    {0b01101000,  2}, {0b01101001,  2}, {0b01101010,  2}, {0b01101011,  2}, {0b01101100, 13}, {0b01101101, 13}, {0b01101110, 13}, {0b01101111, 13},
    
    {0b01010000,  2}, {0b01010001,  2}, {0b01010010, 12}, {0b01010011, 12}, {0b01010100,  2}, {0b01010101,  2}, {0b01010110, 12}, {0b01010111, 12},
    {0b01011000,  2}, {0b01011001,  2}, {0b01011010, 12}, {0b01011011, 12}, {0b01011100,  2}, {0b01011101,  2}, {0b01011110, 12}, {0b01011111, 12},
    
    {0b10010000,  2}, {0b10010001, 11}, {0b10010010,  2}, {0b10010011, 11}, {0b10010100,  2}, {0b10010101, 11}, {0b10010110,  2}, {0b10010111, 11},
    {0b10011000,  2}, {0b10011001, 11}, {0b10011010,  2}, {0b10011011, 11}, {0b10011100,  2}, {0b10011101, 11}, {0b10011110,  2}, {0b10011111, 11},
    
    
    // 三方向
    {0b11100000,  2}, {0b11100001,  2}, {0b11100010,  2}, {0b11100011,  2}, {0b11100100, 13}, {0b11100101, 13}, {0b11100110, 13}, {0b11100111, 13},
    {0b11101000, 14}, {0b11101001, 14}, {0b11101010, 14}, {0b11101011, 14}, {0b11101100,  4}, {0b11101101,  4}, {0b11101110,  4}, {0b11101111,  4},
    
    {0b01110000,  2}, {0b01110001,  2}, {0b01110010, 12}, {0b01110011, 12}, {0b01110100, 13}, {0b01110101, 13}, {0b01110110,  5}, {0b01110111,  5},
    {0b01111000,  2}, {0b01111001,  2}, {0b01111010, 12}, {0b01111011, 12}, {0b01111100, 13}, {0b01111101, 13}, {0b01111110,  5}, {0b01111111,  5},

    {0b11010000,  2}, {0b11010001, 11}, {0b11010010, 12}, {0b11010011,  3}, {0b11010100,  2}, {0b11010101, 11}, {0b11010110, 12}, {0b11010111,  3},
    {0b11011000,  2}, {0b11011001, 11}, {0b11011010, 12}, {0b11011011,  3}, {0b11011100,  2}, {0b11011101, 11}, {0b11011110, 12}, {0b11011111,  3},
    
    {0b10110000,  2}, {0b10110001, 11}, {0b10110010,  2}, {0b10110011, 11}, {0b10110100,  2}, {0b10110101, 11}, {0b10110110,  2}, {0b10110111, 11},
    {0b10111000, 14}, {0b10111001,  6}, {0b10111010, 14}, {0b10111011,  6}, {0b10111100, 14}, {0b10111101,  6}, {0b10111110, 14}, {0b10111111,  6},
    
    // 四边
    {0b11110000,  2}, {0b11110001, 11}, {0b11110010, 12}, {0b11110011,  3}, {0b11110100, 13}, {0b11110101,  2}, {0b11110110,  5}, {0b11110111,  8},
    {0b11111000, 14}, {0b11111001,  6}, {0b11111010,  2}, {0b11111011,  7}, {0b11111100,  4}, {0b11111101,  9}, {0b11111110, 10}, {0b11111111,  1},
};

// map转成数组，增加查询速度
static int* teDirTypeList = nullptr;

static inline int isTeBlock(int te) {
    return (0 < te && te < MAP_CO_DATA_PLAT) ? 1 : 0;
}

static int getTeDirType(int lef, int rig, int top, int bot, int leto, int rito, int lebo, int ribo) {
    int type =
        (isTeBlock(lef)  << 7) +
        (isTeBlock(rig)  << 6) +
        (isTeBlock(top)  << 5) +
        (isTeBlock(bot)  << 4) +
        (isTeBlock(leto) << 3) +
        (isTeBlock(rito) << 2) +
        (isTeBlock(lebo) << 1) +
        (isTeBlock(ribo) << 0);

    if (!teDirTypeList) {
        teDirTypeList = new int[256];
        for (int i = 0; i < 256; i++) {
            teDirTypeList[i] = teDirTypeMap[i];
        }
    }
    
    return teDirTypeList[type];
}

// 地形要根据周围的地形做出调整
static void finishTeDir(AreaTmpData* tmpData) {
    std::vector<std::vector<int>>* pCo = &(tmpData->finalAreaData->co); // 根据碰撞进行变化，所以取co
    std::vector<std::vector<int>>* pTe = &(tmpData->finalAreaData->te);

    // 上下
    std::vector<int>* pHeadLine = &((*pCo)[0]);
    int lSize = (int)pHeadLine->size();

    for (int rx = 0; rx < lSize; rx++) {
        if ((*pTe)[0][rx] > MAP_AUTO_TE_DATA_MAX) continue; // 如果te已经被设置，则跳过
        
        int coData = (*pHeadLine)[rx];
        if (coData == MAP_CO_DATA_BLOCK) {
            if (rx == 0) {
                coData = getTeDirType(1, (*pCo)[0][1], 1, (*pCo)[1][0], 1, 1, (*pCo)[1][1], 1);
            } else if (rx == lSize - 1) {
                coData = getTeDirType((*pCo)[0][rx - 1], 1, 1, (*pCo)[1][rx], 1, 1, 1, (*pCo)[1][rx - 1]);
            } else {
                coData = getTeDirType(
                    (*pCo)[0][rx - 1], (*pCo)[0][rx + 1], 1, (*pCo)[1][rx],
                    1, 1, (*pCo)[1][rx + 1], (*pCo)[1][rx - 1]);
            }
            (*pTe)[0][rx] = coData;
        }
    }

    int hSize = (int)pCo->size();
    int ryF = hSize - 1;
    std::vector<int>* pFinalLine = &((*pCo)[ryF]);
    for (int rx = 0; rx < lSize; rx++) {
        if ((*pTe)[ryF][rx] > MAP_AUTO_TE_DATA_MAX) continue; // 如果te已经被设置，则跳过
        
        int coData = (*pFinalLine)[rx];
        if (coData == MAP_CO_DATA_BLOCK) {
            if (rx == 0) {
                coData = getTeDirType(1, (*pCo)[ryF][1], (*pCo)[ryF - 1][0], 1, 1, (*pCo)[ryF - 1][1], 1, 1);
            } else if (rx == lSize - 1) {
                coData = getTeDirType((*pCo)[ryF][rx - 1], 1, (*pCo)[ryF - 1][rx], 1, (*pCo)[ryF - 1][rx - 1], 1, 1, 1);
            } else {
                coData = getTeDirType(
                    (*pCo)[ryF][rx - 1], (*pCo)[ryF][rx + 1], (*pCo)[ryF - 1][rx], 1,
                    (*pCo)[ryF - 1][rx - 1], (*pCo)[ryF - 1][rx + 1], 1, 1);
            }
            (*pTe)[ryF][rx] = coData;
        }
    }
    
    // 左右
    for (int ry = 1; ry < hSize - 1; ry++) {
        if ((*pTe)[ry][0] <= MAP_AUTO_TE_DATA_MAX) {
            int coDataLeft = (*pCo)[ry][0];
            if (coDataLeft == MAP_CO_DATA_BLOCK) {
                coDataLeft = getTeDirType(
                    1, (*pCo)[ry][1], (*pCo)[ry - 1][0], (*pCo)[ry + 1][0],
                    1, (*pCo)[ry - 1][1], (*pCo)[ry + 1][1], 1);
                (*pTe)[ry][0] = coDataLeft;
            }
        }
        
        if ((*pTe)[ry][lSize - 1] <= MAP_AUTO_TE_DATA_MAX) {
            int coDataRight = (*pCo)[ry][lSize - 1];
            if (coDataRight == MAP_CO_DATA_BLOCK) {
                coDataRight = getTeDirType(
                    (*pCo)[ry][lSize - 2], 1, (*pCo)[ry - 1][lSize - 1], (*pCo)[ry + 1][lSize - 1],
                    (*pCo)[ry - 1][lSize - 2], 1, 1, (*pCo)[ry + 1][lSize - 2]);
                (*pTe)[ry][lSize - 1] = coDataRight;
            }
        }
    }

    // 中间
    for (int ry = 1; ry < hSize - 1; ry++) {
        std::vector<int>* pCoLine = &((*pCo)[ry]);
        for (int rx = 1; rx < lSize - 1; rx++) {
            if ((*pTe)[ry][rx] > MAP_AUTO_TE_DATA_MAX) continue;
            
            int coData = (*pCoLine)[rx];
            if (coData == MAP_CO_DATA_BLOCK) {
                coData = getTeDirType(
                    (*pCo)[ry][rx - 1], (*pCo)[ry][rx + 1], (*pCo)[ry - 1][rx], (*pCo)[ry + 1][rx],
                    (*pCo)[ry - 1][rx - 1], (*pCo)[ry - 1][rx + 1],
                    (*pCo)[ry + 1][rx + 1], (*pCo)[ry + 1][rx - 1]);
                (*pTe)[ry][rx] = coData;
            }
        }
    }
}

static inline bool isTeGround(int teData) {
    return teData == MAP_CO_DATA_PLAT ||
        teData == MAP_CO_DATA_PLAT_HEAD ||
        teData == MAP_CO_DATA_BLOCK_UP;
}

void MapCreator::finishFinalArea(AreaTmpData* tmpData) {
    finishPlatBG(tmpData);
    finishTeDir(tmpData);
    
    printVecVecToFile(tmpData->finalAreaData->te, "myMap/map.csv");
}

// 获取地面信息
void MapCreator::handleGround(AreaTmpData* tmpData) {
    
    // 准备出禁止生成位置的map
    std::map<int, bool> noepMap;
    for (int noep : tmpData->w_curTemp->noeps) {
        noepMap[noep] = true;
    }
    
    std::vector<std::vector<int>>* pTe = &(tmpData->finalAreaData->te);
    
    // 最左右和最上3格不能有敌人
    for (int ry = 3; ry < pTe->size(); ry++) {
        std::vector<int>* pTeLine = &(*pTe)[ry];
        for (int rx = 1; rx < pTeLine->size() - 1; rx++) {
            int teData = (*pTeLine)[rx];
            
            if (!isTeGround(teData)) continue;
            
            int noepData = tmpData->w_curTemp->getNoEnemyData(rx, ry);
            if (noepMap.find(noepData) != noepMap.end()) continue;
            
            int teAboveAbove = (*pTe)[ry - 2][rx];
            if (teAboveAbove != MAP_CO_DATA_BLANK) continue;
            
            // 地面数据放入
            tmpData->finalAreaData->groundInfos.push_back(rx);
            tmpData->finalAreaData->groundInfos.push_back(ry);
            
            int teL = (*pTe)[ry][rx - 1];
            int teR = (*pTe)[ry][rx + 1];
            
            bool wide = isTeGround(teL) && isTeGround(teR);
            bool high = ry > 4 &&
                (*pTe)[ry - 3][rx] == MAP_CO_DATA_BLANK &&
                (*pTe)[ry - 4][rx] == MAP_CO_DATA_BLANK;
            
            int groundType;
            if (!wide && !high) {
                groundType = 1;
            } else if (wide) {
                groundType = 2;
            } else if (high) {
                groundType = 3;
            } else {
                groundType = 4;
            }
            tmpData->finalAreaData->groundInfos.push_back(groundType);
        }
    }
}

// 之所以在最后是因为需要前面的数据
// 包括凸起部分的旋转，
void MapCreator::createExtraPipeSpine(AreaTmpData* tmpData) {
    
//    std::vector<std::vector<int>>* pTe = &(tmpData->finalAreaData->te);
//
//    // 生成spine
//    for (int ry = 3; ry < pTe->size(); ry++) {
//        std::vector<int>* pTeLine = &(*pTe)[ry];
//        for (int rx = 1; rx < pTeLine->size() - 1; rx++) {
//            int teData = (*pTeLine)[rx];
//
//        }
//    }
}

static int* originTilePosNumList = new int[16384];

void MapCreator::addRandomTile(AreaTmpData* tmpData) {
    std::vector<TileSubst*> tileSubsts = tmpData->w_curTemp->areaAttri->tileSubsts;
    std::vector<std::vector<int>>* pTe = &(tmpData->finalAreaData->te);

    int KEY = 1000;

    for (TileSubst* tileSubst: tileSubsts) {
        int origin = tileSubst->origin;
        int tileSize = 0;
        for (int ry = 1; ry < pTe->size(); ry++) {
            std::vector<int>* pTeLine = &(*pTe)[ry];
            for (int rx = 1; rx < pTeLine->size() - 1; rx++) {
                int teData = (*pTeLine)[rx];
                if (teData == origin) {
                    originTilePosNumList[tileSize] = ry * KEY + rx;
                    tileSize++;
                }
            }
        }

        int randomSize = tileSize * tileSubst->ratio / 100;
        std::vector<int> substs = tileSubst->substs;
        int substSize = (int)substs.size();
        for (int _ = 0; _ < randomSize; _++) {
            int random = getRandom(0, tileSize - 1);
            int originTileNum = originTilePosNumList[random];
            int ry = originTileNum / KEY;
            int rx = originTileNum % KEY;

            int subst = substs[getRandom(0, substSize - 1)];
            (*pTe)[ry][rx] = subst;
        }
    }
}

void MapCreator::saveToJsonFile(AreaTmpData* tmpData) {
    FinalAreaData* finalData = tmpData->finalAreaData;
    
    rapidjson::Document finalDataDoc;
    finalDataDoc.SetObject();
    rapidjson::Document::AllocatorType& allocator = finalDataDoc.GetAllocator();

    std::vector<std::vector<int>>* pCo = &(finalData->co);
    
    // wh
    int rW = (int)(*pCo)[0].size();
    finalDataDoc.AddMember("rW", rW, allocator);
    int rH = (int)pCo->size();
    finalDataDoc.AddMember("rH", rH, allocator);
    
    // co
    rapidjson::Value coDoc(rapidjson::kArrayType);

    for (int ry = 0; ry < pCo->size(); ry++) {
        std::vector<int>* pCoLine = &(*pCo)[ry];
        rapidjson::Value coLineDoc(rapidjson::kArrayType);
        for (int rx = 0; rx < pCoLine->size(); rx++) {
            int coData = (*pCoLine)[rx];
            coLineDoc.PushBack(coData, allocator);
        }
        coDoc.PushBack(coLineDoc, allocator);
    }

    finalDataDoc.AddMember("co", coDoc, allocator);

    // te
    std::vector<std::vector<int>>* pTe = &(finalData->te);
    rapidjson::Value teDoc(rapidjson::kArrayType);

    for (int ry = 0; ry < pTe->size(); ry++) {
        std::vector<int>* pTeLine = &(*pTe)[ry];
        rapidjson::Value teLineDoc(rapidjson::kArrayType);
        for (int rx = 0; rx < pTeLine->size(); rx++) {
            int teData = (*pTeLine)[rx];
            teLineDoc.PushBack(teData, allocator);
        }
        teDoc.PushBack(teLineDoc, allocator);
    }

    finalDataDoc.AddMember("te", teDoc, allocator);

    // groundInfos
    std::vector<int>* groundInfos = &(finalData->groundInfos);
    rapidjson::Value groundInfoDoc(rapidjson::kArrayType);

    for (int index = 0; index < groundInfos->size(); index++) {
        int info = (*groundInfos)[index];
        groundInfoDoc.PushBack(info, allocator);
    }

    finalDataDoc.AddMember("groundInfos", groundInfoDoc, allocator);
    
    // spines
    std::vector<SpineData*>* spineList = &(finalData->spineList);
    rapidjson::Value spinesDoc(rapidjson::kArrayType);

    for (int index = 0; index < spineList->size(); index++) {
        SpineData* spineData = (*spineList)[index];
        rapidjson::Value spineDoc(rapidjson::kObjectType);

        spineDoc.AddMember("pX", spineData->pX, allocator);
        spineDoc.AddMember("pY", spineData->pY, allocator);
        spineDoc.AddMember("id", spineData->id, allocator);

        spinesDoc.PushBack(spineDoc, allocator);
    }
    
    finalDataDoc.AddMember("spines", spinesDoc, allocator);

    // 导出
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    finalDataDoc.Accept(writer);

    log("%s", buffer.GetString());

//    auto path = FileUtils::getInstance()->getWritablePath();
//    path.append("myhero.json");
//    FILE* file = fopen(path.c_str(), "wb");
//    if(file) {
//        fputs(buffer.GetString(), file);
//        fclose(file);
//    }
}

MY_SPACE_END

// js绑定工具 ----------------------------------------------------------------

using namespace my;

bool seval_to_vecvec(const se::Value& v, std::vector<std::vector<int>>* ret) {
    assert(ret != nullptr);
    assert(v.isObject());
    se::Object* obj = v.toObject();
    assert(obj->isArray());

    bool ok = true;
    uint32_t len = 0;
    ok = obj->getArrayLength(&len);
    SE_PRECONDITION2(ok, false, "error vecvec len");

    se::Value tmp;
    for (uint32_t i = 0; i < len; ++i) {
        ok = obj->getArrayElement(i, &tmp);
        SE_PRECONDITION2(ok && tmp.isObject(), false, "error vecvec tmp");

        se::Object* subObj = tmp.toObject();
        assert(subObj->isArray());

        uint32_t subLen = 0;
        ok = subObj->getArrayLength(&subLen);
        SE_PRECONDITION2(ok, false, "error vecvec sublen");

        std::vector<int> subVec;
        se::Value subTmp;
        for (uint32_t j = 0; j < subLen; ++j) {
            ok = subObj->getArrayElement(j, &subTmp);
            SE_PRECONDITION2(ok && subTmp.isNumber(), false, "error vecvec num");

            int num = subTmp.toInt32();
            subVec.push_back(num);
        }

        ret->push_back(subVec);
    }

    return true;
}

bool seval_to_mapelebase(const se::Value& v, MapEleBase* ret) {
    assert(v.isObject() && ret != nullptr);
    se::Object* obj = v.toObject();

    se::Value tW;
    se::Value tH;
    se::Value te;
    se::Value co;
    se::Value door;

    bool ok;

    // w and h
    ok = obj->getProperty("tW", &tW);
    SE_PRECONDITION2(ok && tW.isNumber(), false, "error tW");
    ret->tW = tW.toInt32();

    ok = obj->getProperty("tH", &tH);
    SE_PRECONDITION2(ok && tH.isNumber(), false, "error tH");
    ret->tH = tH.toInt32();

    // co
    ok = obj->getProperty("co", &co);
    SE_PRECONDITION2(ok, false, "error co");

    ok = seval_to_vecvec(co, &ret->co);
    SE_PRECONDITION2(ok, false, "error co res");

    return true;
}

bool seval_to_spine(const se::Value& v, SpineData* spineData) {
    bool ok;
    se::Object* subobj = v.toObject();
    
    se::Value pX;
    se::Value pY;
    se::Value id;
    
    ok = subobj->getProperty("pX", &pX);
    SE_PRECONDITION2(ok && pX.isNumber(), false, "error spine x");
    spineData->pX = pX.toInt32();
    
    ok = subobj->getProperty("pY", &pY);
    SE_PRECONDITION2(ok && pY.isNumber(), false, "error spine y");
    spineData->pY = pY.toInt32();
    
    ok = subobj->getProperty("id", &id);
    SE_PRECONDITION2(ok && id.isNumber(), false, "error spine id");
    spineData->id = id.toInt32();
    
    return true;
}

bool seval_to_mapele(const se::Value& v, MapEle* ret) {
    assert(v.isObject() && ret != nullptr);
    se::Object* obj = v.toObject();

    bool ok;
    uint32_t len = 0;
    se::Value tmp;

    assert(obj->isArray());
    ok = obj->getArrayLength(&len);
    SE_PRECONDITION2(ok, false, "error mapele len");
    assert(len == 7);

    se::Value baseIndex;
    se::Value tW;
    se::Value tH;
    se::Value usingTXs;
    se::Value usingTYs;
    se::Value door;
    se::Value spines;

    // index w h
    ok = obj->getArrayElement(0, &baseIndex);
    SE_PRECONDITION2(ok && baseIndex.isNumber(), false, "error baseIndex");
    ret->baseIndex = baseIndex.toInt32();

    ok = obj->getArrayElement(1, &tW);
    SE_PRECONDITION2(ok && tW.isNumber(), false, "error mapele tW");
    ret->tW = tW.toInt32();

    ok = obj->getArrayElement(2, &tH);
    SE_PRECONDITION2(ok && tH.isNumber(), false, "error mapele tH");
    ret->tH = tH.toInt32();

    ok = obj->getArrayElement(3, &usingTXs);
    SE_PRECONDITION2(ok && usingTXs.isNumber(), false, "error usingTXs");
    ret->usingTXs = usingTXs.toInt32();

    ok = obj->getArrayElement(4, &usingTYs);
    SE_PRECONDITION2(ok && usingTYs.isNumber(), false, "error usingTYs");
    ret->usingTYs = usingTYs.toInt32();

    // door
    ok = obj->getArrayElement(5, &door);
    SE_PRECONDITION2(ok && door.isObject(), false, "error door");

    se::Object* doorObj = door.toObject();
    assert(doorObj->isArray());
    ok = doorObj->getArrayLength(&len);
    SE_PRECONDITION2(ok, false, "error door len");
    assert(len == 4); // 上下左右，只能是4个

    for (uint32_t i = 0; i < len; ++i) {
        ok = doorObj->getArrayElement(i, &tmp);
        SE_PRECONDITION2(ok && tmp.isObject(), false, "error door tmp");

        se::Object* subObj = tmp.toObject();
        assert(subObj->isArray());

        uint32_t subLen = 0;
        ok = subObj->getArrayLength(&subLen);
        SE_PRECONDITION2(ok, false, "error door sublen");

        std::vector<int> subVec;
        se::Value subTmp;
        for (uint32_t j = 0; j < subLen; ++j) {
            ok = subObj->getArrayElement(j, &subTmp);
            SE_PRECONDITION2(ok && subTmp.isNumber(), false, "error door sub tmp");
            subVec.push_back(subTmp.toInt32());
        }

        ret->door[i] = subVec;
    }
    
    // spines
    ok = obj->getArrayElement(6, &spines);
    SE_PRECONDITION2(ok && spines.isObject(), false, "error spine");
    
    se::Object* spinesObj = spines.toObject();
    assert(spinesObj->isArray());
    ok = spinesObj->getArrayLength(&len);
    SE_PRECONDITION2(ok, false, "error spine len");
    
    for (uint32_t i = 0; i < len; ++i) {
        ok = spinesObj->getArrayElement(i, &tmp);
        SE_PRECONDITION2(ok && tmp.isObject(), false, "error spine tmp");
        
        SpineData* spineData = new SpineData();
        ok = seval_to_spine(tmp, spineData);
        SE_PRECONDITION2(ok, false, "error spine");
        
        ret->spineList.push_back(spineData);
    }

    return true;
}

bool seval_to_fitemp(const se::Value& v, FiTemp* ret) {
    assert(v.isObject() && ret != nullptr);
    se::Object* obj = v.toObject();

    se::Value rX;
    se::Value rY;
    se::Value rW;
    se::Value rH;
    se::Value tX;
    se::Value tY;
    se::Value tW;
    se::Value tH;
    se::Value te;
    se::Value co;
    se::Value door;
    se::Value substitutes;

    bool ok;
    uint32_t len = 0;

    // w and h x y
    ok = obj->getProperty("rX", &rX);
    SE_PRECONDITION2(ok && rX.isNumber(), false, "error rX");
    ret->rX = rX.toInt32();

    ok = obj->getProperty("rY", &rY);
    SE_PRECONDITION2(ok && rY.isNumber(), false, "error rY");
    ret->rY = rY.toInt32();

    ok = obj->getProperty("rW", &rW);
    SE_PRECONDITION2(ok && rW.isNumber(), false, "error rW");
    ret->rW = rW.toInt32();

    ok = obj->getProperty("rH", &rH);
    SE_PRECONDITION2(ok && rH.isNumber(), false, "error rH");
    ret->rH = rH.toInt32();

    ok = obj->getProperty("tX", &tX);
    SE_PRECONDITION2(ok && tX.isNumber(), false, "error tX");
    ret->tX = tX.toInt32();

    ok = obj->getProperty("tY", &tY);
    SE_PRECONDITION2(ok && tY.isNumber(), false, "error tY");
    ret->tY = tY.toInt32();

    ok = obj->getProperty("tW", &tW);
    SE_PRECONDITION2(ok && tW.isNumber(), false, "error tW");
    ret->tW = tW.toInt32();

    ok = obj->getProperty("tH", &tH);
    SE_PRECONDITION2(ok && tH.isNumber(), false, "error tH");
    ret->tH = tH.toInt32();

    // te co
    ok = obj->getProperty("te", &te);
    SE_PRECONDITION2(ok, false, "error te");

    ok = seval_to_vecvec(te, &ret->te);
    SE_PRECONDITION2(ok, false, "error te res");

    ok = obj->getProperty("co", &co);
    SE_PRECONDITION2(ok, false, "error co");

    ok = seval_to_vecvec(co, &ret->co);
    SE_PRECONDITION2(ok, false, "error co res");

    // door
    ok = obj->getProperty("door", &door);
    SE_PRECONDITION2(ok && door.isObject(), false, "error door");

    se::Object* doorObj = door.toObject();
    assert(doorObj->isArray());
    ok = doorObj->getArrayLength(&len);
    SE_PRECONDITION2(ok, false, "error door len");
    assert(len == 4); // 上下左右，只能是4个

    se::Value tmp;
    for (uint32_t i = 0; i < len; ++i) {
        ok = doorObj->getArrayElement(i, &tmp);
        SE_PRECONDITION2(ok && tmp.isObject(), false, "error door tmp");

        se::Object* subObj = tmp.toObject();
        assert(subObj->isArray());

        uint32_t subLen = 0;
        ok = subObj->getArrayLength(&subLen);
        SE_PRECONDITION2(ok, false, "error door sublen");

        std::vector<int> subVec;
        se::Value subTmp;
        for (uint32_t j = 0; j < subLen; ++j) {
            ok = subObj->getArrayElement(j, &subTmp);
            SE_PRECONDITION2(ok && subTmp.isNumber(), false, "error door sub tmp");
            subVec.push_back(subTmp.toInt32());
        }

        ret->door[i] = subVec;
    }

    // sub
    ok = obj->getProperty("substitutes", &substitutes);
    SE_PRECONDITION2(ok && substitutes.isObject(), false, "error substitutes");

    se::Object* substitutesObj = substitutes.toObject();
    assert(substitutesObj->isArray());
    ok = substitutesObj->getArrayLength(&len);
    SE_PRECONDITION2(ok, false, "error substitutes len");
    assert(len == 4); // 上下左右，只能是4个

    se::Value substitutesTmp;
    for (uint32_t i = 0; i < len; ++i) {
        ok = substitutesObj->getArrayElement(i, &substitutesTmp);
        SE_PRECONDITION2(ok && substitutesTmp.isNumber(), false, "error substitutestmp sub tmp");
        ret->substitutes[i] = substitutesTmp.toInt32();
    }

    return true;
}

bool seval_to_maptemp(const se::Value& v, AreaTemp* ret) {
    assert(v.isObject() && ret != nullptr);
    se::Object* obj = v.toObject();

    se::Value rW;
    se::Value rH;
    se::Value noeps;
    se::Value fis;
    se::Value ra;
    se::Value spines;
    se::Value attri;

    bool ok;
    uint32_t len = 0;

    // rW and rH
    ok = obj->getProperty("rW", &rW);
    SE_PRECONDITION2(ok && rW.isNumber(), false, "error rW");
    ret->rW = rW.toInt32();

    ok = obj->getProperty("rH", &rH);
    SE_PRECONDITION2(ok && rH.isNumber(), false, "error rH");
    ret->rH = rH.toInt32();

    // noeps
    ok = obj->getProperty("noeps", &noeps);
    SE_PRECONDITION2(ok && noeps.isObject(), false, "error noeps");
    se::Object* noepsObj = noeps.toObject();
    assert(noepsObj->isArray());

    ok = noepsObj->getArrayLength(&len);
    SE_PRECONDITION2(ok, false, "error noeps len");

    se::Value tmp;
    for (uint32_t i = 0; i < len; ++i) {
        ok = noepsObj->getArrayElement(i, &tmp);
        SE_PRECONDITION2(ok && tmp.isNumber(), false, "error noeps obj");
        ret->noeps.push_back(tmp.toInt32());
    }

    // fis
    ok = obj->getProperty("fis", &fis);
    SE_PRECONDITION2(ok && fis.isObject(), false, "error fis");
    
    se::Object* fisObj = fis.toObject();
    assert(fisObj->isArray());
    ok = fisObj->getArrayLength(&len);
    SE_PRECONDITION2(ok, false, "error fisObj len");

    se::Value ft;
    for (uint32_t i = 0; i < len; ++i) {
        ok = fisObj->getArrayElement(i, &ft);
        SE_PRECONDITION2(ok, false, "error fisObj obj");

        FiTemp* fiTemp = new FiTemp();
        ok = seval_to_fitemp(ft, fiTemp);
        SE_PRECONDITION2(ok, false, "error fisObj fiTemp");

        ret->fis.push_back(fiTemp);
    }

    // ra
    ok = obj->getProperty("ra", &ra);
    SE_PRECONDITION2(ok, false, "error ra");

    ok = seval_to_vecvec(ra, &ret->ra);
    SE_PRECONDITION2(ok, false, "error ra res");
    
    // spines
    ok = obj->getProperty("spines", &spines);
    SE_PRECONDITION2(ok && spines.isObject(), false, "error spine");
    
    se::Object* spinesObj = spines.toObject();
    assert(spinesObj->isArray());
    ok = spinesObj->getArrayLength(&len);
    SE_PRECONDITION2(ok, false, "error spine len");
    
    for (uint32_t i = 0; i < len; ++i) {
        ok = spinesObj->getArrayElement(i, &tmp);
        SE_PRECONDITION2(ok && tmp.isObject(), false, "error spine tmp");
        
        SpineData* spineData = new SpineData();
        ok = seval_to_spine(tmp, spineData);
        SE_PRECONDITION2(ok, false, "error spine");
        
        ret->spineList.push_back(spineData);
    }
    
    // attri
    ok = obj->getProperty("attri", &attri);
    SE_PRECONDITION2(ok && attri.isObject(), false, "error attri");
    
    se::Object* attriObj = attri.toObject();
    AreaAttri* attriData = new AreaAttri();
    
    se::Value holeRatio;
    se::Value tileSubsts;
    
    ok = attriObj->getProperty("holeRatio", &holeRatio);
    SE_PRECONDITION2(ok && holeRatio.isNumber(), false, "error holeRatio");
    attriData->holeRatio = holeRatio.toInt32();
    
    ok = attriObj->getProperty("tileSubsts", &tileSubsts);
    SE_PRECONDITION2(ok && tileSubsts.isObject(), false, "error tileSubsts");
    
    se::Object* tileSubstsObj = tileSubsts.toObject();
    assert(tileSubstsObj->isArray());
    ok = tileSubstsObj->getArrayLength(&len);
    SE_PRECONDITION2(ok, false, "error tileSubstsObj len");
    
    for (uint32_t i = 0; i < len; ++i) {
        ok = tileSubstsObj->getArrayElement(i, &tmp);
        SE_PRECONDITION2(ok && tmp.isObject(), false, "error tileSubstsObj tmp");
        
        se::Object* substObj = tmp.toObject();
        TileSubst* substData = new TileSubst();
        
        se::Value origin;
        se::Value substs;
        se::Value ratio;
        
        ok = substObj->getProperty("origin", &origin);
        SE_PRECONDITION2(ok && origin.isNumber(), false, "error substObj origin");
        substData->origin = origin.toInt32();
        
        ok = substObj->getProperty("substs", &substs);
        SE_PRECONDITION2(ok && substs.isObject(), false, "error substObj substs");
        
        se::Object* substsObj = substs.toObject();
        assert(substsObj->isArray());
        ok = substsObj->getArrayLength(&len);
        SE_PRECONDITION2(ok, false, "error substsObj len");
        
        se::Value substTmp;
        for (uint32_t j = 0; j < len; ++j) {
            ok = substsObj->getArrayElement(j, &substTmp);
            SE_PRECONDITION2(ok && substTmp.isNumber(), false, "error substsObj tmp");
            substData->substs.push_back(substTmp.toInt32());
        }
        
        ok = substObj->getProperty("ratio", &ratio);
        SE_PRECONDITION2(ok && ratio.isNumber(), false, "error substObj ratio");
        substData->ratio = ratio.toInt32();

        attriData->tileSubsts.push_back(substData);
    }
    
    ret->areaAttri = attriData;

    return true;
}

// js绑定 -------------------------------------------------------------------

se::Object* __jsb_my_MapCreator_proto = nullptr;
se::Class* __jsb_my_MapCreator_class = nullptr;

static bool jsb_my_MapCreator_getInstance(se::State& s)
{
    const auto& args = s.args();
    size_t argc = args.size();
    CC_UNUSED bool ok = true;
    if (argc == 0) {
        auto result = MapCreator::getInstance();
        se::Value instanceVal;
        native_ptr_to_seval<MapCreator>(result, __jsb_my_MapCreator_class, &instanceVal);
        instanceVal.toObject()->root();
        s.rval() = instanceVal;
        return true;
    }
    SE_REPORT_ERROR("wrong number of arguments: %d, was expecting %d", (int)argc, 0);
    return false;
}
SE_BIND_FUNC(jsb_my_MapCreator_getInstance)

static bool jsb_my_MapCreator_addMapEleBase(se::State& s) {
    MapCreator* cobj = (MapCreator*)s.nativeThisObject();
    SE_PRECONDITION2(cobj, false, "jsb_my_MapCreator_addMapEleBase : Invalid Native Object");

    const auto& args = s.args();
    size_t argc = args.size();
    CC_UNUSED bool ok = true;
    if (argc == 1) {
        MapEleBase* arg0 = new MapEleBase();
        ok &= seval_to_mapelebase(args[0], arg0);
        SE_PRECONDITION2(ok, false, "jsb_my_MapCreator_addMapEleBase : Error processing arguments");

        cobj->addMapEleBase(arg0);
        return true;
    }
    SE_REPORT_ERROR("wrong number of arguments: %d, was expecting %d", (int)argc, 1);
    return false;
}
SE_BIND_FUNC(jsb_my_MapCreator_addMapEleBase);

static bool jsb_my_MapCreator_addMapEle(se::State& s) {
    MapCreator* cobj = (MapCreator*)s.nativeThisObject();
    SE_PRECONDITION2(cobj, false, "jsb_my_MapCreator_addMapEle : Invalid Native Object");

    const auto& args = s.args();
    size_t argc = args.size();
    CC_UNUSED bool ok = true;
    if (argc == 1) {
        MapEle* arg0 = new MapEle();
        ok &= seval_to_mapele(args[0], arg0);
        SE_PRECONDITION2(ok, false, "jsb_my_MapCreator_addMapEle : Error processing arguments");

        cobj->addMapEle(arg0);
        return true;
    }
    SE_REPORT_ERROR("wrong number of arguments: %d, was expecting %d", (int)argc, 1);
    return false;
}
SE_BIND_FUNC(jsb_my_MapCreator_addMapEle);

static bool jsb_my_MapCreator_addMapEleIndexs(se::State& s) {
    MapCreator* cobj = (MapCreator*)s.nativeThisObject();
    SE_PRECONDITION2(cobj, false, "jsb_my_MapCreator_addMapEleIndexs : Invalid Native Object");

    const auto& args = s.args();
    size_t argc = args.size();
    CC_UNUSED bool ok = true;
    if (argc == 5) {
        int arg0 = 0;
        ok &= seval_to_int32(args[0], (int32_t*)&arg0);
        SE_PRECONDITION2(ok, false, "jsb_my_MapCreator_addMapEleIndexs : Error processing arguments 0");

        int arg1 = 0;
        ok &= seval_to_int32(args[1], (int32_t*)&arg1);
        SE_PRECONDITION2(ok, false, "jsb_my_MapCreator_addMapEleIndexs : Error processing arguments 1");

        int arg2 = 0;
        ok &= seval_to_int32(args[2], (int32_t*)&arg2);
        SE_PRECONDITION2(ok, false, "jsb_my_MapCreator_addMapEleIndexs : Error processing arguments 2");

        int arg3 = 0;
        ok &= seval_to_int32(args[3], (int32_t*)&arg3);
        SE_PRECONDITION2(ok, false, "jsb_my_MapCreator_addMapEleIndexs : Error processing arguments 3");

        std::vector<int> arg4;
        ok &= seval_to_std_vector_int(args[4], (std::vector<int> *)&arg4);
        SE_PRECONDITION2(ok, false, "jsb_my_MapCreator_addMapEleIndexs : Error processing arguments 4");

        cobj->addMapEleIndexs(arg0, arg1, arg2, arg3, arg4);
        return true;
    }
    SE_REPORT_ERROR("wrong number of arguments: %d, was expecting %d", (int)argc, 1);
    return false;
}
SE_BIND_FUNC(jsb_my_MapCreator_addMapEleIndexs);

static bool jsb_my_MapCreator_addAreaTemp(se::State& s) {
    MapCreator* cobj = (MapCreator*)s.nativeThisObject();
    SE_PRECONDITION2(cobj, false, "jsb_my_MapCreator_addAreaTemp : Invalid Native Object");

    const auto& args = s.args();
    size_t argc = args.size();
    CC_UNUSED bool ok = true;
    if (argc == 2) {
        int arg0 = 0;
        AreaTemp* arg1 = new AreaTemp();

        ok &= seval_to_int32(args[0], (int32_t*)&arg0);
        SE_PRECONDITION2(ok, false, "jsb_my_MapCreator_addAreaTemp : Error processing arguments 0");

        ok &= seval_to_maptemp(args[1], arg1);
        SE_PRECONDITION2(ok, false, "jsb_my_MapCreator_addAreaTemp : Error processing arguments 1");

        cobj->addAreaTemp(arg0, arg1);
        return true;
    }
    SE_REPORT_ERROR("wrong number of arguments: %d, was expecting %d", (int)argc, 2);
    return false;
}
SE_BIND_FUNC(jsb_my_MapCreator_addAreaTemp)

static bool jsb_my_MapCreator_createArea(se::State& s) {
    MapCreator* cobj = (MapCreator*)s.nativeThisObject();
    SE_PRECONDITION2(cobj, false, "jsb_my_MapCreator_createArea : Invalid Native Object");

    const auto& args = s.args();
    size_t argc = args.size();
    CC_UNUSED bool ok = true;
    if (argc == 2) {
        int arg0 = 0;
        std::function<void(bool)> arg1 = nullptr;

        ok &= seval_to_int32(args[0], (int32_t*)&arg0);
        SE_PRECONDITION2(ok, false, "jsb_my_MapCreator_createArea : Error processing arguments 0");

        do {
            if (args[1].isObject() && args[1].toObject()->isFunction()) {
                se::Value jsThis(s.thisObject());
                se::Value jsFunc(args[1]);
                jsThis.toObject()->attachObject(jsFunc.toObject());
                auto lambda = [=](bool larg0) -> void {
                    se::ScriptEngine::getInstance()->clearException();
                    se::AutoHandleScope hs;

                    CC_UNUSED bool ok = true;
                    se::ValueArray args;
                    args.resize(1);
                    ok &= boolean_to_seval(larg0, &args[0]);
                    se::Value rval;
                    se::Object* thisObj = jsThis.isObject() ? jsThis.toObject() : nullptr;
                    se::Object* funcObj = jsFunc.toObject();
                    bool succeed = funcObj->call(args, thisObj, &rval);
                    if (!succeed) {
                        se::ScriptEngine::getInstance()->clearException();
                    }
                };
                arg1 = lambda;
            } else {
                ok = false;
            }
        } while(false);

        SE_PRECONDITION2(ok, false, "jsb_my_MapCreator_createArea : Error processing arguments");

        cobj->createArea(arg0, arg1);
        return true;
    }
    SE_REPORT_ERROR("wrong number of arguments: %d, was expecting %d", (int)argc, 2);
    return false;
}
SE_BIND_FUNC(jsb_my_MapCreator_createArea)

bool register_my_map_creator(se::Object* obj) {
    // 命名空间
    se::Value nsVal;
    if (!obj->getProperty("my", &nsVal))
    {
        se::HandleObject jsobj(se::Object::createPlainObject());
        nsVal.setObject(jsobj);
        obj->setProperty("my", nsVal);
    }
    se::Object* ns = nsVal.toObject();

    // 生成类
    auto cls = se::Class::create("MapCreator", ns, nullptr, nullptr);

    cls->defineStaticFunction("getInstance", _SE(jsb_my_MapCreator_getInstance));
    cls->defineFunction("addMapEleBase", _SE(jsb_my_MapCreator_addMapEleBase));
    cls->defineFunction("addMapEle", _SE(jsb_my_MapCreator_addMapEle));
    cls->defineFunction("addMapEleIndexs", _SE(jsb_my_MapCreator_addMapEleIndexs));
    cls->defineFunction("addAreaTemp", _SE(jsb_my_MapCreator_addAreaTemp));
    cls->defineFunction("createArea", _SE(jsb_my_MapCreator_createArea));
    cls->install();
    JSBClassType::registerClass<MapCreator>(cls);

    __jsb_my_MapCreator_proto = cls->getProto();
    __jsb_my_MapCreator_class = cls;

    se::ScriptEngine::getInstance()->clearException();
    return true;
}
