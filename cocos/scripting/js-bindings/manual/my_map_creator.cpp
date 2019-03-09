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

#define MY_SPACE_BEGIN   namespace my {
#define MY_SPACE_END     }

USING_NS_CC;

// 测试函数 ---------------------------------------------------------------

static int64_t getCurrentTime() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static void printVecVec(std::vector<std::vector<int>> &vecvec) {
    printf("vvvvvvvvvvvvvvv\n");
    for (int i = 0; i < vecvec.size(); i++) {
        std::vector<int> vec = vecvec[i];
        for (int j = 0; j < vec.size(); j++) {
            printf("%05d, ", vec[j]);
        }
        printf("\n");
    }
    printf("^^^^^^^^^^^^^^^\n");
}

// 获取随机数 -----------------------------------------------------------

static void getReadyForRandom() {
    int seed = (int)time(0);
    printf("s = %d\n", seed);
    srand(1552108943);
}

static inline int getRandom(int from, int to) {
    return (rand() % (to - from + 1)) + from;
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

    std::vector<int> list[MAX_R_TW][MAX_R_TH][MAX_DOOR_TYPE]; // 对应不同w，h以及门方向的随机区块使用的配置，最大宽高是固定的
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

// 地图模板
class MapTemp {
public:
    MapTemp();
    virtual ~MapTemp();

    int rW;
    int rH;

    std::vector<int> noeps; // 无敌人位置（***每个int 为 x * NO_ENEMY_KEY + y 组成）
    std::vector<FiTemp*> fis; // 固定块
    std::vector<std::vector<int>> ra; // 随机块

    int getNoEnemyPosX(int p) {
        return p % NO_ENEMY_KEY;
    }

    int getNoEnemeyPosY(int p) {
        return p - getNoEnemyPosX(p);
    }
};

// 要发送到js的数据 -----------------------------------------------------------

class MapData {
public:
    MapData();
    virtual ~MapData();

    std::vector<std::vector<int>> te; // 地形
    std::vector<std::vector<int>> co; // 碰撞
    std::vector<int> groundInfos; // 地面信息（***每3个int一组，分别是x，y，type）
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
    HoleDir dir;
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

    bool inCircuit; // 是否在通路中

    int doorDir;
    std::map<int, int> substitutesMap; // HoleDir: HoleDir

    std::vector<HoleRelation*> relations;

    MapEle* ele;
};

class MapTmpData {
public:
    MapTmpData();
    virtual ~MapTmpData();

    int tW;
    int tH;

    std::vector<std::vector<int>> holeTMap;
    std::vector<HoleData*> holeVec;
    std::vector<PipeData*> pipeVec;

    int curSceneKey;
    MapTemp* w_curTemp; // 弱引用的当前地图模板
};

// 地图生成器 ----------------------------------------------------------------

#define FI_HOLE_ID_BEGIN (1000)
#define RA_HOLE_ID_BEGIN (2000)
#define FI_EDGE_ID_BEGIN (100)
#define RA_EDGE_ID_BEGIN (200)

class MapCreator {
public:
    MapCreator();
    virtual ~MapCreator();

    static MapCreator* getInstance();

    // 载入区块元素
    void addMapEleBase(const MapEleBase* mapEleBase);
    void addMapEle(const MapEle* mapEle);
    void addMapEleIndex(const int sceneKey, const int tW, const int tH, const int doorType, const int eleIndex);

    // 读取模板
    void addMapTemp(const int sceneKey, const MapTemp* mapTemp);

    // 生成地图，然后从回调传出
    void createMap(const int sceneKey, const std::function<void(bool)>& callback);

protected:
    void init();
    void threadLoop();

    void initTmpData(MapTmpData* tmpData);
    void digHole(MapTmpData* tmpData);
    void calcHoleRelation(MapTmpData* tmpData);
    void connectAllHole(MapTmpData* tmpData);
    void connectExtraHole(MapTmpData* tmpData);
    void assignEleToHole(MapTmpData* tmpData);
    void digPipe(MapTmpData* tmpData);

private:
    bool _creating;

    std::thread _thread;
    std::mutex _sleepMutex;
    std::condition_variable _sleepCondition;

    // 输入数据
    std::vector<MapEleBase*> _mapEleBaseVec;
    std::vector<MapEle*> _mapEleVec;
    std::map<int, MapEleList*> _mapEleListMap; // 不同场景Key对应的元素清单

    std::map<int, MapTemp*> _mapTempMap; // 不同场景Key对应的地图模板

    int _curSceneKey;
    std::function<void(bool)> _callback;
};

// 实现 --------------------------------------------------------------

MapEleBase::MapEleBase() {
}

MapEleBase::~MapEleBase() {
}

// ---------------

MapEle::MapEle() {
}

MapEle::~MapEle() {
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

MapTemp::MapTemp() {
}

MapTemp::~MapTemp() {
    for (FiTemp* fi: fis) {
        delete fi;
    }
}

// ---------------

MapData::MapData() {
}

MapData::~MapData() {
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
inCircuit(false), doorDir(0), tX(itX), tY(itY), tW(itW), tH(itH), type(itype), index(iindex) {
}

HoleData::~HoleData() {
    for (HoleRelation* r : relations) {
        delete r;
    }
}

MapTmpData::MapTmpData() {
}

MapTmpData::~MapTmpData() {
    for (HoleData* hole: holeVec) {
        delete hole;
    }
    for (PipeData* pipe: pipeVec) {
        delete pipe;
    }
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

    std::map<int, MapEleList*>::iterator it;
    for(it = _mapEleListMap.begin(); it != _mapEleListMap.end();) {
        delete it->second;
        _mapEleListMap.erase(it++);
    }

    std::map<int, MapTemp*>::iterator it2;
    for(it2 = _mapTempMap.begin(); it2 != _mapTempMap.end();) {
        delete it2->second;
        _mapTempMap.erase(it2++);
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

void MapCreator::addMapEleIndex(const int sceneKey, const int tW, const int tH, const int doorType, const int eleIndex) {
    if (_mapEleListMap.find(sceneKey) == _mapEleListMap.end()) {
        MapEleList* list = new MapEleList();
        list->list[tW][tH][doorType].push_back(eleIndex);
        _mapEleListMap[sceneKey] = list;
    } else {
        _mapEleListMap[sceneKey]->list[tW][tH][doorType].push_back(eleIndex);
    }
}

void MapCreator::addMapTemp(const int sceneKey, const MapTemp* mapTemp) {
    _mapTempMap[sceneKey] = const_cast<MapTemp*>(mapTemp);
}

void MapCreator::createMap(const int sceneKey, const std::function<void(bool)>& callback) {
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

        MapTmpData* tmpData = new MapTmpData();
        MapData* mapData = new MapData();

        tmpData->curSceneKey = _curSceneKey;
        tmpData->w_curTemp = _mapTempMap[_curSceneKey];

        initTmpData(tmpData);
        digHole(tmpData);
        calcHoleRelation(tmpData);
        connectAllHole(tmpData);
        connectExtraHole(tmpData);
        assignEleToHole(tmpData);

        // mapData 保存到本地 todo

        // 释放
        delete mapData;
        delete tmpData;

        // 结束
        auto sch = cocos2d::Director::getInstance()->getScheduler();
        sch->performFunctionInCocosThread([=]() {
            _creating = false;
            _callback(true);
        });
    }
}

void MapCreator::initTmpData(MapTmpData* tmpData) {
    tmpData->tW = (int)(tmpData->w_curTemp->ra[0].size());
    tmpData->tH = (int)(tmpData->w_curTemp->ra.size());

    // 初始化一个矩阵，记录已经使用了的block，未使用为0，使用了为1
    std::vector<std::vector<int>> copyRa(tmpData->w_curTemp->ra);
    tmpData->holeTMap = std::move(copyRa);
}

// 在地图上填数据 （无边缘检测）
static void setMap(std::vector<std::vector<int>> &data, int beginX, int beginY, int edgeW, int edgeH, int key) {
    for (int i = 0; i < edgeW; i++) {
        int curX = beginX + i;
        for (int j = 0; j < edgeH; j++) {
            int curY = beginY + j;
            data[curY][curX] = key;
        }
    }
}

// 把地图空的地方填上数据 （带边缘检测）
static void setBlankMap(std::vector<std::vector<int>> &data, int beginX, int beginY, int edgeW, int edgeH, int key) {
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

void MapCreator::digHole(MapTmpData* tmpData) {
    int mapTW = tmpData->tW;
    int mapTH = tmpData->tH;
    auto holeTMap = std::move(tmpData->holeTMap); // 右值引用，拉出来做处理，之后再放回去

    int mapTMax = mapTW * mapTH;
    float holeRatio = 0.3; //llytodo 要从js传入
    int holeBlockSize = (int)((float)mapTMax * holeRatio);

    int holeIndex = 0;

    // 处理固定块 并把固定块镶边
    for (FiTemp* fi : tmpData->w_curTemp->fis) {
        setMap(holeTMap, fi->tX, fi->tY, fi->tW, fi->tH, FI_HOLE_ID_BEGIN + holeIndex);
        setBlankMap(holeTMap, fi->tX - 1, fi->tY - 1, fi->tW + 2, fi->tH + 2, FI_EDGE_ID_BEGIN + holeIndex); // 镶边

        auto holeData = new HoleData(fi->tX, fi->tY, fi->tW, fi->tH, HoleType::fi, holeIndex);
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
        int value = holeTMap[ty][tx];

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
                value = holeTMap[ty][tx];
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
            if (subCurX < 0 || holeTMap[ty].size() <= subCurX) break;

            int curValue = holeTMap[ty][subCurX];
            if (curValue == 0) curTX = subCurX;
            else break;
        }

        // 获取高度
        int holeTH = 1;
        for (; holeTH < holeTHMax; holeTH++) {
            int subCurY = ty + holeTH * creatingDir;
            if (subCurY < 0 || holeTMap.size() <= subCurY) break;

            int curValue = holeTMap[subCurY][tx];
            if (curValue == 0) curTY = subCurY;
            else break;
        }

        // 检测另一边是否有阻挡
        for (int holeW2 = 1; holeW2 < holeTW; holeW2++) {
            int curX2 = tx + holeW2 * creatingDir;
            int curValue = holeTMap[curTY][curX2];
            if (curValue == 0) curTX = curX2;
            else {
                holeTW = holeW2;
                break;
            }
        }

        for (int holeH2 = 1; holeH2 < holeTH; holeH2++) {
            int curY2 = ty + holeH2 * creatingDir;
            int curValue = holeTMap[curY2][curTX];
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
                    if (holeTMap[ty + i * creatingDir][tx - creatingDir] != 0) {
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
                    if (holeTMap[ty - creatingDir][tx + i * creatingDir] != 0) {
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
        setMap(holeTMap, beginX, beginY, holeTW, holeTH, RA_HOLE_ID_BEGIN + holeIndex);
        setBlankMap(holeTMap, beginX - 1, beginY - 1, holeTW + 2, holeTH + 2, RA_EDGE_ID_BEGIN + holeIndex); // 镶边
        tmpData->holeVec.push_back(new HoleData(beginX, beginY, holeTW, holeTH, HoleType::ra, holeIndex));

        // 检测是否完成
        holeBlockSize -= (holeTW * holeTH);
        if (holeBlockSize <= 0) break;

        holeIndex++;
    }

    tmpData->holeTMap = std::move(holeTMap); // 处理后的数据放回原处
    printVecVec(tmpData->holeTMap);
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
void MapCreator::calcHoleRelation(MapTmpData* tmpData) {
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

static HoleDir getOppositeDir(HoleDir dir) {
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
        case HoleDir::lef_top: return (getRandom(0, 1) == 1 ? HoleDir::lef_mid : HoleDir::mid_top);
        case HoleDir::lef_bot: return (getRandom(0, 1) == 1 ? HoleDir::lef_mid : HoleDir::mid_bot);
        case HoleDir::rig_top: return (getRandom(0, 1) == 1 ? HoleDir::rig_mid : HoleDir::mid_top);
        case HoleDir::rig_bot: return (getRandom(0, 1) == 1 ? HoleDir::rig_mid : HoleDir::mid_bot);
        default: return dir;
    }
}

static int getDoorDirFromStraightHoleDir(HoleDir dir) {
    switch (dir) {
        case HoleDir::lef_mid: return DOOR_LEFT;
        case HoleDir::rig_mid: return DOOR_RIGHT;
        case HoleDir::mid_top: return DOOR_UP;
        case HoleDir::mid_bot: return DOOR_DOWN;
        default: return 0; // 不会到这里
    }
}

static HoleDir getHoleDirFromDoorDir(int dir) {
    switch (dir) {
        case DOOR_LEFT: return HoleDir::lef_mid;
        case DOOR_RIGHT: return HoleDir::rig_mid;
        case DOOR_UP: return HoleDir::mid_top;
        case DOOR_DOWN: return HoleDir::mid_bot;
        default: return HoleDir::lef_mid; // 不会到这里
    }
}

static PipeEndPoint* createPipeEndPoint(HoleData* hole, HoleDir dir) {
    PipeEndPoint* endPoint = new PipeEndPoint();

    endPoint->holeIndex = hole->index;

    if (hole->type == HoleType::fi) { // 固定块如果有不能连接的方向，则用另一个方向替代
        int ddir = getDoorDirFromStraightHoleDir(getStraightHoleDir(dir));
        int substitutesDir = hole->substitutesMap.find(ddir)->second; // 必定存在
        endPoint->dir = getHoleDirFromDoorDir(substitutesDir);
    } else {
        endPoint->dir = getStraightHoleDir(dir);
        hole->doorDir |= getDoorDirFromStraightHoleDir(endPoint->dir);
    }

    return endPoint;
}

static PipeData* createPipe(MapTmpData* tmpData, HoleRelation* relation) {
    HoleData* my = tmpData->holeVec[relation->myHoleIndex];
    HoleData* another = tmpData->holeVec[relation->anoHoleIndex];
    HoleDir dir = relation->dir;
    HoleDir oppositeDir = getOppositeDir(relation->dir);

    PipeData* pipe = new PipeData();
    pipe->endPoints[0] = createPipeEndPoint(my, dir);
    pipe->endPoints[1] = createPipeEndPoint(another, oppositeDir);
    return pipe;
}

static void dealRelationPathForConnection(MapTmpData* tmpData, HoleRelation* relation, HoleData* anoHole);

static void dealEachRelationForConnection(MapTmpData* tmpData, HoleData* hole) {
    for (HoleRelation* relation : hole->relations) {
        HoleData* anoHole = tmpData->holeVec[relation->anoHoleIndex];
        if (anoHole->inCircuit) continue;
        dealRelationPathForConnection(tmpData, relation, anoHole);
    }
}

static void dealRelationPathForConnection(MapTmpData* tmpData, HoleRelation* relation, HoleData* anoHole) {
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
void MapCreator::connectAllHole(MapTmpData* tmpData) {
    HoleData* hole = tmpData->holeVec[0];
    hole->inCircuit = true;
    dealEachRelationForConnection(tmpData, hole);
}

// 生成额外的通路，使表现更丰富，通路可通可不通
void MapCreator::connectExtraHole(MapTmpData* tmpData) {
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
        pipe->connected = getRandom(0, 1) == 1;
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

void MapCreator::assignEleToHole(MapTmpData* tmpData) {
    MapEleList* list = this->_mapEleListMap[tmpData->curSceneKey];

    for (HoleData* hole : tmpData->holeVec) {
        if (hole->type == HoleType::fi) continue;
        EleDoorType eleDoorType = getEleDirTypesFromHoleDoorDir(hole->doorDir);

        std::vector<int> eleList = list->list[hole->tW - 1][hole->tH - 1][(int)eleDoorType];
        int index = getRandom(0, (int)eleList.size() - 1);
        int eleIndex = eleList[index];
        hole->ele = this->_mapEleVec[eleIndex];
    }
}

void MapCreator::digPipe(MapTmpData* tmpData) {
    // 遍历所有管道，根据其两端门的位置，产生管道坐标
    for(PipeData* pipe: tmpData->pipeVec) {
        PipeEndPoint* endPoint0 = pipe->endPoints[0];
        PipeEndPoint* endPoint1 = pipe->endPoints[1];

    }
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

        se::Object* subobj = tmp.toObject();
        assert(subobj->isArray());

        uint32_t sublen = 0;
        ok = subobj->getArrayLength(&sublen);
        SE_PRECONDITION2(ok, false, "error vecvec sublen");

        std::vector<int> subVec;
        se::Value subtmp;
        for (uint32_t j = 0; j < sublen; ++j) {
            ok = subobj->getArrayElement(j, &subtmp);
            SE_PRECONDITION2(ok && subtmp.isNumber(), false, "error vecvec num");

            int num = subtmp.toInt32();
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

bool seval_to_mapele(const se::Value& v, MapEle* ret) {
    assert(v.isObject() && ret != nullptr);
    se::Object* obj = v.toObject();

    se::Value baseIndex;
    se::Value tW;
    se::Value tH;
    se::Value usingTXs;
    se::Value usingTYs;
    se::Value door;

    bool ok;
    uint32_t len = 0;

    // index x y w h
    ok = obj->getProperty("baseIndex", &baseIndex);
    SE_PRECONDITION2(ok && baseIndex.isNumber(), false, "error baseIndex");
    ret->baseIndex = baseIndex.toInt32();

    ok = obj->getProperty("tW", &tW);
    SE_PRECONDITION2(ok && tW.isNumber(), false, "error mapele tW");
    ret->tW = tW.toInt32();

    ok = obj->getProperty("tH", &tH);
    SE_PRECONDITION2(ok && tH.isNumber(), false, "error mapele tH");
    ret->tH = tH.toInt32();

    ok = obj->getProperty("usingTXs", &usingTXs);
    SE_PRECONDITION2(ok && usingTXs.isNumber(), false, "error usingTXs");
    ret->usingTXs = usingTXs.toInt32();

    ok = obj->getProperty("usingTYs", &usingTYs);
    SE_PRECONDITION2(ok && usingTYs.isNumber(), false, "error usingTYs");
    ret->usingTYs = usingTYs.toInt32();

    // door
    ok = obj->getProperty("doorType", &door);
    SE_PRECONDITION2(ok && door.isObject(), false, "error door");

    se::Object* doorobj = door.toObject();
    assert(doorobj->isArray());
    ok = doorobj->getArrayLength(&len);
    SE_PRECONDITION2(ok, false, "error door len");
    assert(len == 4); // 上下左右，只能是4个

    se::Value tmp;
    for (uint32_t i = 0; i < len; ++i) {
        ok = doorobj->getArrayElement(i, &tmp);
        SE_PRECONDITION2(ok && tmp.isObject(), false, "error door tmp");

        se::Object* subobj = tmp.toObject();
        assert(subobj->isArray());

        uint32_t sublen = 0;
        ok = subobj->getArrayLength(&sublen);
        SE_PRECONDITION2(ok, false, "error door sublen");

        std::vector<int> subVec;
        se::Value subtmp;
        for (uint32_t j = 0; j < sublen; ++j) {
            ok = subobj->getArrayElement(j, &subtmp);
            SE_PRECONDITION2(ok && subtmp.isNumber(), false, "error door sub tmp");
            subVec.push_back(subtmp.toInt32());
        }

        ret->door[i] = subVec;
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

    se::Object* doorobj = door.toObject();
    assert(doorobj->isArray());
    ok = doorobj->getArrayLength(&len);
    SE_PRECONDITION2(ok, false, "error door len");
    assert(len == 4); // 上下左右，只能是4个

    se::Value tmp;
    for (uint32_t i = 0; i < len; ++i) {
        ok = doorobj->getArrayElement(i, &tmp);
        SE_PRECONDITION2(ok && tmp.isObject(), false, "error door tmp");

        se::Object* subobj = tmp.toObject();
        assert(subobj->isArray());

        uint32_t sublen = 0;
        ok = subobj->getArrayLength(&sublen);
        SE_PRECONDITION2(ok, false, "error door sublen");

        std::vector<int> subVec;
        se::Value subtmp;
        for (uint32_t j = 0; j < sublen; ++j) {
            ok = subobj->getArrayElement(j, &subtmp);
            SE_PRECONDITION2(ok && subtmp.isNumber(), false, "error door sub tmp");
            subVec.push_back(subtmp.toInt32());
        }

        ret->door[i] = subVec;
    }

    // sub
    ok = obj->getProperty("substitutes", &substitutes);
    SE_PRECONDITION2(ok && substitutes.isObject(), false, "error substitutes");

    se::Object* substitutesobj = substitutes.toObject();
    assert(substitutesobj->isArray());
    ok = substitutesobj->getArrayLength(&len);
    SE_PRECONDITION2(ok, false, "error substitutes len");
    assert(len == 4); // 上下左右，只能是4个

    se::Value substitutestmp;
    for (uint32_t i = 0; i < len; ++i) {
        ok = substitutesobj->getArrayElement(i, &substitutestmp);
        SE_PRECONDITION2(ok && substitutestmp.isNumber(), false, "error substitutestmp sub tmp");
        ret->substitutes[i] = substitutestmp.toInt32();
    }

    return true;
}

bool seval_to_maptemp(const se::Value& v, MapTemp* ret) {
    assert(v.isObject() && ret != nullptr);
    se::Object* obj = v.toObject();

    se::Value rW;
    se::Value rH;
    se::Value noeps;
    se::Value fis;
    se::Value ra;

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

static bool jsb_my_MapCreator_addMapEleIndex(se::State& s) {
    MapCreator* cobj = (MapCreator*)s.nativeThisObject();
    SE_PRECONDITION2(cobj, false, "jsb_my_MapCreator_addMapEleIndex : Invalid Native Object");

    const auto& args = s.args();
    size_t argc = args.size();
    CC_UNUSED bool ok = true;
    if (argc == 5) {
        int arg0 = 0;
        ok &= seval_to_int32(args[0], (int32_t*)&arg0);
        SE_PRECONDITION2(ok, false, "jsb_my_MapCreator_addMapEleIndex : Error processing arguments 0");

        int arg1 = 0;
        ok &= seval_to_int32(args[1], (int32_t*)&arg1);
        SE_PRECONDITION2(ok, false, "jsb_my_MapCreator_addMapEleIndex : Error processing arguments 1");

        int arg2 = 0;
        ok &= seval_to_int32(args[2], (int32_t*)&arg2);
        SE_PRECONDITION2(ok, false, "jsb_my_MapCreator_addMapEleIndex : Error processing arguments 2");

        int arg3 = 0;
        ok &= seval_to_int32(args[3], (int32_t*)&arg3);
        SE_PRECONDITION2(ok, false, "jsb_my_MapCreator_addMapEleIndex : Error processing arguments 3");

        int arg4 = 0;
        ok &= seval_to_int32(args[4], (int32_t*)&arg4);
        SE_PRECONDITION2(ok, false, "jsb_my_MapCreator_addMapEleIndex : Error processing arguments 4");

        cobj->addMapEleIndex(arg0, arg1, arg2, arg3, arg4);
        return true;
    }
    SE_REPORT_ERROR("wrong number of arguments: %d, was expecting %d", (int)argc, 1);
    return false;
}
SE_BIND_FUNC(jsb_my_MapCreator_addMapEleIndex);

static bool jsb_my_MapCreator_addMapTemp(se::State& s) {
    MapCreator* cobj = (MapCreator*)s.nativeThisObject();
    SE_PRECONDITION2(cobj, false, "jsb_my_MapCreator_addMapEle : Invalid Native Object");

    const auto& args = s.args();
    size_t argc = args.size();
    CC_UNUSED bool ok = true;
    if (argc == 2) {
        int arg0 = 0;
        MapTemp* arg1 = new MapTemp();

        ok &= seval_to_int32(args[0], (int32_t*)&arg0);
        SE_PRECONDITION2(ok, false, "jsb_my_MapCreator_createMap : Error processing arguments 0");

        ok &= seval_to_maptemp(args[1], arg1);
        SE_PRECONDITION2(ok, false, "jsb_my_MapCreator_createMap : Error processing arguments 1");

        cobj->addMapTemp(arg0, arg1);
        return true;
    }
    SE_REPORT_ERROR("wrong number of arguments: %d, was expecting %d", (int)argc, 2);
    return false;
}
SE_BIND_FUNC(jsb_my_MapCreator_addMapTemp)

static bool jsb_my_MapCreator_createMap(se::State& s) {
    MapCreator* cobj = (MapCreator*)s.nativeThisObject();
    SE_PRECONDITION2(cobj, false, "jsb_my_MapCreator_addMapEle : Invalid Native Object");

    const auto& args = s.args();
    size_t argc = args.size();
    CC_UNUSED bool ok = true;
    if (argc == 2) {
        int arg0 = 0;
        std::function<void(bool)> arg1 = nullptr;

        ok &= seval_to_int32(args[0], (int32_t*)&arg0);
        SE_PRECONDITION2(ok, false, "jsb_my_MapCreator_createMap : Error processing arguments 0");

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

        SE_PRECONDITION2(ok, false, "jsb_my_MapCreator_createMap : Error processing arguments");

        cobj->createMap(arg0, arg1);
        return true;
    }
    SE_REPORT_ERROR("wrong number of arguments: %d, was expecting %d", (int)argc, 2);
    return false;
}
SE_BIND_FUNC(jsb_my_MapCreator_createMap)

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
    cls->defineFunction("addMapEleIndex", _SE(jsb_my_MapCreator_addMapEleIndex));
    cls->defineFunction("addMapTemp", _SE(jsb_my_MapCreator_addMapTemp));
    cls->defineFunction("createMap", _SE(jsb_my_MapCreator_createMap));
    cls->install();
    JSBClassType::registerClass<MapCreator>(cls);

    __jsb_my_MapCreator_proto = cls->getProto();
    __jsb_my_MapCreator_class = cls;

    se::ScriptEngine::getInstance()->clearException();
    return true;
}
