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

// 数据模型 ---------------------------------------------------------------

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

MY_SPACE_BEGIN

// 从js层获得的数据 -----------------------------------------------------------

// 随机区域中使用的区块模板的基础块
class MapEleBase {
public:
    MapEleBase();
    virtual ~MapEleBase();

    int tW;
    int tH;
    std::vector<std::vector<int>> te; // 地形
    std::vector<std::vector<int>> co; // 碰撞
};

// 随机区域中使用的区块模板，从Base中扣出来
class MapEle {
public:
    MapEle();
    virtual ~MapEle();

    int baseIndex; // base序号
    int usingTXs; // 横向使用的块 如 110011，就是使用两边各两个
    int usingTYs; // 纵向使用的块

    std::vector<int> door[4]; // 上，下，左，右 的固定块的连接方向（***每2个int一组，分别是x，y）

    int getDoorLen(int key) {
        return (int)door[key].size() / 2;
    }

    int getDoorX(int key, int index) {
        return door[key][index * 2];
    }

    int getDoorY(int key, int index) {
        return door[key][index * 2 + 1];
    }
};

#define MAX_R_TW (6)
#define MAX_R_TH (6)
#define MAX_DOOR_TYPE (8)

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
    std::vector<int> door[4]; // 上，下，左，右 的固定块的连接方向（***每2个int一组，分别是x，y）

    int getDoorLen(int key) {
        return (int)door[key].size() / 2;
    }

    int getDoorX(int key, int index) {
        return door[key][index * 2];
    }

    int getDoorY(int key, int index) {
        return door[key][index * 2 + 1];
    }
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

// 连接hole的通道
class PipeData {
public:
    PipeData();
    virtual ~PipeData();

    int holeIndex[2];
};

// 标记其他hole的位置关系
class HoleRelation {
public:
    HoleRelation();
    virtual ~HoleRelation();

    HoleDir dir;
    float distance;

    int myHoleIndex;
    int anoHoleIndex;

    int pipeIndex;
};

class HoleData{
public:
    HoleData();
    virtual ~HoleData();

    HoleData(int itX, int itY, int itW, int itH, int itype, int iindex);

    int tX;
    int tY;
    int tW;
    int tH;

    int type; // 1 fixed 2 ra
    int index;

    int doorDir;

    std::vector<HoleRelation*> relations;
};

class MapTmpData {
public:
    MapTmpData();
    virtual ~MapTmpData();

    int blockW;
    int blockH;

    std::vector<std::vector<int>> holeTMap;
    std::vector<HoleData*> holeVec;
    std::vector<PipeData*> pipeVec;
};

// 地图生成器 ----------------------------------------------------------------

#define FI_HOLE_ID_BEGIN (10000)
#define RA_HOLE_ID_BEGIN (20000)
#define FI_EDGE_ID_BEGIN (1000)
#define RA_EDGE_ID_BEGIN (2000)

class MapCreator {
public:
    MapCreator();
    virtual ~MapCreator();

    static MapCreator* getInstance();

    // 载入区块元素
    void addMapEleBase(const MapEleBase* mapEleBase);
    void addMapEle(const MapEle* mapEle);
    void addMapEleIndex(const int sceneKey, const int tW, const int tH, const int doorType, const int eleIndex);

    // 读取模板，生成地图，然后从回调传出
    void createMap(const int sceneKey, const MapTemp* mapBase, const std::function<void(MapData*)>& callback);

protected:
    void init();
    void threadLoop();

    void initTmpData(MapTmpData* tmpData);
    void digHole(MapTmpData* tmpData);
    void designatedDoorDirForHole(MapTmpData* tmpData);

    int getRandom(int from, int to);
private:
    bool _creating;

    std::thread _thread;
    std::mutex _sleepMutex;
    std::condition_variable _sleepCondition;

    // 输入数据
    std::vector<MapEleBase*> _mapEleBaseVec;
    std::vector<MapEle*> _mapEleVec;
    std::map<int, MapEleList*> _mapEleListMap; // 不同场景Key（场景key = 场景id * 10 + 小场景id）对应的元素清单

    int _curSceneKey;
    MapTemp* _mapBase;
    std::function<void(MapData*)> _callback;

    // 输出数据
    MapData* _mapData;
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

PipeData::PipeData() {
}

PipeData::~PipeData() {
}

// ---------------

HoleRelation::HoleRelation() {
}

HoleRelation::~HoleRelation() {
}

// ---------------

HoleData::HoleData() {
}

HoleData::~HoleData() {
    for (HoleRelation* r : relations) {
        delete r;
    }
}

HoleData::HoleData(int itX, int itY, int itW, int itH, int itype, int iindex):
tX(itX), tY(itY), tW(itW), tH(itH), type(itype), index(iindex) {
    doorDir = 0;
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
_creating(false),
_mapBase(nullptr),
_mapData(nullptr) {
    srand((int)time(0));
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

    if (_mapBase) delete _mapBase;
    if (_mapData) delete _mapData;
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

void MapCreator::createMap(const int sceneKey, const MapTemp* mapBase, const std::function<void(MapData*)>& callback) {
    if (_creating) return;
    _creating = true;

    if (_mapBase != nullptr) {
        delete _mapBase;
    }

    if (_mapData != nullptr) {
        delete _mapData;
        _mapData = nullptr;
    }

    _curSceneKey = sceneKey;
    _mapBase = const_cast<MapTemp*>(mapBase);
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

        initTmpData(tmpData);
        digHole(tmpData);
        designatedDoorDirForHole(tmpData);

        // 结束
        auto sch = cocos2d::Director::getInstance()->getScheduler();
        sch->performFunctionInCocosThread([=]() {
            _creating = false;
//            _callback(_mapData);
        });

        delete tmpData;
    }
}

void MapCreator::initTmpData(MapTmpData* tmpData) {
    tmpData->blockW = (int)_mapBase->ra[0].size();
    tmpData->blockH = (int)_mapBase->ra.size();

    // 初始化一个矩阵，记录已经使用了的block，未使用为0，使用了为1
    std::vector<std::vector<int>> copyRa(_mapBase->ra);
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
    int blockW = tmpData->blockW;
    int blockH = tmpData->blockH;
    auto holeTMap = tmpData->holeTMap;

    int blockMax = tmpData->blockW * tmpData->blockH;
    float holeRatio = 0.3; //llytodo 要从js传入
    int holeBlockSize = (int)((float)blockMax * holeRatio);

    int holeIndex = 0;

    // 处理固定块 并把固定块镶边
    for (FiTemp* fi : _mapBase->fis) {
        setMap(holeTMap, fi->tX, fi->tY, fi->tW, fi->tH, FI_HOLE_ID_BEGIN + holeIndex);
        setBlankMap(holeTMap, fi->tX - 1, fi->tY - 1, fi->tW + 2, fi->tH + 2, FI_EDGE_ID_BEGIN + holeIndex); // 镶边

        auto holeData = new HoleData(fi->tX, fi->tY, fi->tW, fi->tH, 1, holeIndex);
        if (fi->door[0].size() > 0) holeData->doorDir |= DOOR_UP;
        if (fi->door[1].size() > 0) holeData->doorDir |= DOOR_DOWN;
        if (fi->door[2].size() > 0) holeData->doorDir |= DOOR_LEFT;
        if (fi->door[3].size() > 0) holeData->doorDir |= DOOR_RIGHT;
        tmpData->holeVec.push_back(holeData);
        holeIndex++;
    }

    // 开始挖坑
    int creatingDir = -1; // 挖坑方向

    while (true) {
        // 反转挖坑方向，为了让效果更平均，所以从不同的方向挖倔
        creatingDir *= -1;

        // 随机获取一个位置
        int tx = getRandom(0, blockW - 1);
        int ty = getRandom(0, blockH - 1);
        int value = holeTMap[ty][tx];

        if (value != 0) { // 如果所取位置已经使用过，则获取另一个位置，但保证尽量在有限的随机次数内完成
            tx = blockW - 1 - tx; // 从对称位置开始
            ty = blockH - 1 - ty;
            while (true) {
                tx += creatingDir;
                if (creatingDir > 0) {
                    if (tx >= blockW) {
                        tx = 0;
                        ty += creatingDir;
                        if (ty >= blockH) continue;
                    }
                } else {
                    if (tx < 0) {
                        tx = blockW - 1;
                        ty += creatingDir;
                        if (ty < 0) continue;
                    }
                }
                value = holeTMap[ty][tx];
                if (value == 0) break;
            }
        }

        // 获得随机宽高最大值
        int holeTWMax = getRandom(3, MAX_R_TW);
        int holeTHMax = getRandom(3, MAX_R_TW);

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
                bool canExtand = true;
                for (int i = 0; i < holeTH; i++) {
                    if (holeTMap[ty + i][tx - creatingDir] != 0) {
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
                bool canExtand = true;
                for (int i = 0; i < holeTW; i++) {
                    if (holeTMap[ty - creatingDir][tx + i] != 0) {
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
        tmpData->holeVec.push_back(new HoleData(beginX, beginY, holeTW, holeTH, 2, holeIndex));

        // 检测是否完成
        holeBlockSize -= (holeTW * holeTH);
        if (holeBlockSize <= 0) break;

        holeIndex++;
    }

    printVecVec(holeTMap);
}

void MapCreator::designatedDoorDirForHole(MapTmpData* tmpData) {
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
            float distance;
            if (centerTX + halfTW < anoCenterTX - anoHalfTW) {
                float wDis = (anoCenterTX - anoHalfTW) - (centerTX + halfTW);
                if (centerTY + halfTH < anoCenterTY - anoHalfTH) {
                    holeDir = HoleDir::rig_bot;
                    distance = (anoCenterTY - anoHalfTH) - (centerTY + halfTH) + wDis;
                } else if (centerTY - halfTH > anoCenterTY + anoHalfTH) {
                    holeDir = HoleDir::rig_top;
                    distance = (centerTY - halfTH) - (anoCenterTY + anoHalfTH) + wDis;
                } else {
                    holeDir = HoleDir::rig_mid;
                    distance = wDis;
                }
            } else if (centerTX - halfTW > anoCenterTX + anoHalfTW) {
                float wDis = (centerTX - halfTW) - (anoCenterTX + anoHalfTW);
                if (centerTY + halfTH < anoCenterTY - anoHalfTH) {
                    holeDir = HoleDir::lef_bot;
                    distance = (anoCenterTY - anoHalfTH) - (centerTY + halfTH) + wDis;
                } else if (centerTY - halfTH > anoCenterTY + anoHalfTH) {
                    holeDir = HoleDir::lef_top;
                    distance = (centerTY - halfTH) - (anoCenterTY + anoHalfTH) + wDis;
                }else {
                    holeDir = HoleDir::lef_mid;
                    distance = wDis;
                }
            } else {
                if (centerTY < anoCenterTY) {
                    holeDir = HoleDir::mid_bot;
                    distance = (anoCenterTY - anoHalfTH) - (centerTY + halfTH);
                } else {
                    holeDir = HoleDir::mid_top;
                    distance = (centerTY - halfTH) - (anoCenterTY + anoHalfTH);
                }
            }

            // 排序
            HoleRelation* relation = new HoleRelation();
            relation->dir = holeDir;
            relation->distance = distance;
            relation->myHoleIndex = myIndex;
            relation->anoHoleIndex = anoIndex;

            int relationIndex = 0;
            while (true) {
                if (relationIndex == hole->relations.size()) {
                    hole->relations.push_back(relation);
                    break;
                }

                HoleRelation* anoRelation = hole->relations[relationIndex];

                bool sameDir = (anoRelation->dir == relation->dir);
                if (relation->distance < anoRelation->distance) {
                    HoleRelation* tmpRelation = relation;
                    relation = anoRelation;
                    hole->relations[relationIndex] = tmpRelation;
                }

                if (sameDir) { // 不能有同方向的关系，如果方向相同则结束
                    delete relation;
                    break;
                }

                relationIndex++;
            }
        }
    }

    log(">>>>");

}

int MapCreator::getRandom(int from, int to) {
    return (rand() % (to - from + 1)) + from;
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

    // te co
    ok = obj->getProperty("te", &te);
    SE_PRECONDITION2(ok, false, "error te");

    ok = seval_to_vecvec(te, &ret->te);
    SE_PRECONDITION2(ok, false, "error te res");

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
    se::Value usingTXs;
    se::Value usingTYs;
    se::Value door;

    bool ok;
    uint32_t len = 0;

    // index x y
    ok = obj->getProperty("baseIndex", &baseIndex);
    SE_PRECONDITION2(ok && baseIndex.isNumber(), false, "error baseIndex");
    ret->baseIndex = baseIndex.toInt32();

    ok = obj->getProperty("usingTXs", &usingTXs);
    SE_PRECONDITION2(ok && usingTXs.isNumber(), false, "error usingTXs");
    ret->usingTXs = usingTXs.toInt32();

    ok = obj->getProperty("usingTYs", &usingTYs);
    SE_PRECONDITION2(ok && usingTYs.isNumber(), false, "error usingTYs");
    ret->usingTYs = usingTYs.toInt32();

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

bool vecvec_to_seval(const std::vector<std::vector<int>>& v, se::Value* ret) {
    assert(ret != nullptr);
    se::HandleObject obj(se::Object::createArrayObject(v.size()));
    bool ok = true;

    uint32_t i = 0;
    for (const std::vector<int>& value : v) {
        se::Value tmp;
        se::HandleObject subobj(se::Object::createArrayObject(value.size()));

        uint32_t j = 0;
        for (const int subvalue : value) {
            if(!subobj->setArrayElement(j, se::Value(subvalue))) {
                ok = false;
                break;
            }
            ++j;
        }
        tmp.setObject(subobj);

        if (!obj->setArrayElement(i, tmp)) {
            ok = false;
            break;
        }
        ++i;
    }

    if (ok)
        ret->setObject(obj);

    return ok;
}

bool mapdata_to_seval(const MapData* v, se::Value* ret) {
    assert(v != nullptr && ret != nullptr);
    se::HandleObject obj(se::Object::createPlainObject());

    se::Value te;
    vecvec_to_seval(v->te, &te);
    obj->setProperty("te", te);

    se::Value co;
    vecvec_to_seval(v->co, &co);
    obj->setProperty("co", co);

    se::HandleObject groundobj(se::Object::createArrayObject(v->groundInfos.size()));
    bool ok = true;

    uint32_t i = 0;
    for (const int value : v->groundInfos)
    {
        if(!groundobj->setArrayElement(i, se::Value(value))) {
            ok = false;
            break;
        }
        ++i;
    }
    se::Value groundtmp;
    groundtmp.setObject(groundobj);
    obj->setProperty("groundInfos", groundtmp);

    ret->setObject(obj);
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

static bool jsb_my_MapCreator_createMap(se::State& s) {
    MapCreator* cobj = (MapCreator*)s.nativeThisObject();
    SE_PRECONDITION2(cobj, false, "jsb_my_MapCreator_addMapEle : Invalid Native Object");

    const auto& args = s.args();
    size_t argc = args.size();
    CC_UNUSED bool ok = true;
    if (argc == 3) {
        int arg0 = 0;
        MapTemp* arg1 = new MapTemp();
        std::function<void(MapData*)> arg2 = nullptr;

        ok &= seval_to_int32(args[0], (int32_t*)&arg0);
        SE_PRECONDITION2(ok, false, "jsb_my_MapCreator_createMap : Error processing arguments 0");

        ok &= seval_to_maptemp(args[1], arg1);
        SE_PRECONDITION2(ok, false, "jsb_my_MapCreator_createMap : Error processing arguments 1");

        do {
            if (args[2].isObject() && args[2].toObject()->isFunction()) {
                se::Value jsThis(s.thisObject());
                se::Value jsFunc(args[2]);
                jsThis.toObject()->attachObject(jsFunc.toObject());
                auto lambda = [=](MapData* larg0) -> void {
                    se::ScriptEngine::getInstance()->clearException();
                    se::AutoHandleScope hs;

                    CC_UNUSED bool ok = true;
                    se::ValueArray rargs;
                    rargs.resize(1);
                    ok &= mapdata_to_seval(larg0, &rargs[0]);
                    se::Value rval;
                    se::Object* thisObj = jsThis.isObject() ? jsThis.toObject() : nullptr;
                    se::Object* funcObj = jsFunc.toObject();
                    bool succeed = funcObj->call(rargs, thisObj, &rval);
                    if (!succeed) {
                        se::ScriptEngine::getInstance()->clearException();
                    }
                };
                arg2 = lambda;
            } else {
                ok = false;
            }
        } while(false);

        SE_PRECONDITION2(ok, false, "jsb_my_MapCreator_createMap : Error processing arguments");

        cobj->createMap(arg0, arg1, arg2);
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
    cls->defineFunction("createMap", _SE(jsb_my_MapCreator_createMap));
    cls->install();
    JSBClassType::registerClass<MapCreator>(cls);

    __jsb_my_MapCreator_proto = cls->getProto();
    __jsb_my_MapCreator_class = cls;

    se::ScriptEngine::getInstance()->clearException();
    return true;
}
