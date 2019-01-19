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
            printf("%04d, ", vec[j]);
        }
        printf("\n");
    }
    printf("^^^^^^^^^^^^^^^\n");
}

MY_SPACE_BEGIN

// 生成的临时数据 -----------------------------------------------------------

class HoleData{

};

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

// 地图生成器 ----------------------------------------------------------------

#define MAX_R_TW (6)
#define MAX_R_TH (6)
#define MAX_DOOR_TYPE (8)

class MapCreator {
public:
    MapCreator();
    virtual ~MapCreator();

    static MapCreator* getInstance();

    // 载入区块元素
    void addMapEleBase(const MapEleBase* mapEleBase);
    void addMapEle(const MapEle* mapEle);
    void addMapEleIndex(const int tW, const int tH, const int doorType, const int eleIndex);

    // 读取模板，生成地图，然后从回调传出
    void createMap(const MapTemp* mapBase, const std::function<void(MapData*)>& callback);

protected:
    void init();
    void threadLoop();
    void digHole();

    int getRandom(int from, int to);
    void eachMapBlock(std::vector<std::vector<int>> &vecvec, const std::function<void(int x, int y, int value)>& callback);
private:
    bool _creating;

    std::thread _thread;
    std::mutex _sleepMutex;
    std::condition_variable _sleepCondition;

    // 输入数据
    std::vector<MapEleBase*> _mapEleBaseVec;
    std::vector<MapEle*> _mapEleVec;
    std::vector<int> _mapEleIndexs[MAX_R_TW][MAX_R_TH][MAX_DOOR_TYPE]; // 对应不同w，h以及门方向的随机区块使用的配置，最大宽高是固定的

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

void MapCreator::addMapEleIndex(const int tW, const int tH, const int doorType, const int eleIndex) {
    _mapEleIndexs[tW][tH][doorType].push_back(eleIndex);
}

void MapCreator::createMap(const MapTemp* mapBase, const std::function<void(MapData*)>& callback) {
    if (_creating) return;
    _creating = true;

    if (_mapBase != nullptr) {
        delete _mapBase;
    }

    if (_mapData != nullptr) {
        delete _mapData;
        _mapData = nullptr;
    }

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



        digHole();

        // 结束
        auto sch = cocos2d::Director::getInstance()->getScheduler();
        sch->performFunctionInCocosThread([=]() {
            _creating = false;
//            _callback(_mapData);
        });
    }
}

void MapCreator::digHole() {
    // 计算hole应该生成的数量
    int blockW = (int)_mapBase->ra[0].size();
    int blockH = (int)_mapBase->ra.size();
    int blockMax = blockW * blockH;

    float holeRatio = 0.5; //llytodo 要从js传入
    int holeBlockSize = (int)(blockMax * holeRatio);

    // 初始化一个矩阵，记录已经使用了的block，未使用为0，使用了为1
    std::vector<std::vector<int>> holeMap(_mapBase->ra);
    int creatingDir = -1;
    int holeIndex = 0;

    // 开始挖坑
    while (true) {
        creatingDir *= -1;

        int x = getRandom(0, blockW - 1);
        int y = getRandom(0, blockH - 1);

        int value = holeMap[y][x];
        if (value == 0) {
            // 获得随机宽高最大值 更集中在中间的值
            int holeWMax = getRandom(0, 1) > 0 ? getRandom(1, MAX_R_TW) : getRandom(2, MAX_R_TW - 1);
            int holeHMax = getRandom(0, 1) > 0 ? getRandom(1, MAX_R_TH) : getRandom(2, MAX_R_TH - 1);

            int curX = x;
            int curY = y;

            // 获取宽度
            int holeW = 1;
            for (; holeW < holeWMax; holeW++) {
                int subCurX = x + holeW * creatingDir;
                if (subCurX < 0 || holeMap[y].size() <= subCurX) break;

                int curValue = holeMap[y][subCurX];
                if (curValue == 0) curX = subCurX;
                else break;
            }

            // 获取高度
            int holeH = 1;
            for (; holeH < holeHMax; holeH++) {
                int subCurY = y + holeH * creatingDir;
                if (subCurY < 0 || holeMap.size() <= subCurY) break;

                int curValue = holeMap[subCurY][x];
                if (curValue == 0) curY = subCurY;
                else break;
            }

            // 检测另一边是否有阻挡
            for (int holeW2 = 1; holeW2 < holeW; holeW2++) {
                int curX2 = x + holeW2 * creatingDir;
                int curValue = holeMap[curY][curX2];
                if (curValue == 0) curX = curX2;
                else {
                    holeW = holeW2;
                    break;
                }
            }

            for (int holeH2 = 1; holeH2 < holeH; holeH2++) {
                int curY2 = y + holeH2 * creatingDir;
                int curValue = holeMap[curY2][curX];
                if (curValue == 0) curY = curY2;
                else {
                    holeH = holeH2;
                    break;
                }
            }

            // 记录新hole
            int beginX = creatingDir > 0 ? x : curX;
            int beginY = creatingDir > 0 ? y : curY;
            for (int i = 0; i < holeW; i++) {
                for (int j = 0; j < holeH; j++) {
                    holeMap[beginY + j][beginX + i] = 1000 + holeIndex;
                }
            }
            holeIndex++;

            // 检测是否完成
            holeBlockSize -= (holeW * holeH);
            if (holeBlockSize <= 0) break;

        } else {

        }
    }
}

int MapCreator::getRandom(int from, int to) {
    return (rand() % (to - from + 1)) + from;
}

void MapCreator::eachMapBlock(std::vector<std::vector<int>> &vecvec, const std::function<void(int x, int y, int value)>& callback) {
    for (int i = 0; i < vecvec.size(); i++) {
        std::vector<int> vec = vecvec[i];
        for (int j = 0; j < vec.size(); j++) {
            callback(j, i, vec[j]);
        }
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
    if (argc == 3) {
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

        cobj->addMapEleIndex(arg0, arg1, arg2, arg3);
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
    if (argc == 2) {
        MapTemp* arg0 = new MapTemp();
        std::function<void(MapData*)> arg1 = nullptr;

        ok &= seval_to_maptemp(args[0], arg0);

        do {
            if (args[1].isObject() && args[1].toObject()->isFunction()) {
                se::Value jsThis(s.thisObject());
                se::Value jsFunc(args[1]);
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
    cls->defineFunction("createMap", _SE(jsb_my_MapCreator_createMap));
    cls->install();
    JSBClassType::registerClass<MapCreator>(cls);

    __jsb_my_MapCreator_proto = cls->getProto();
    __jsb_my_MapCreator_class = cls;

    se::ScriptEngine::getInstance()->clearException();
    return true;
}
