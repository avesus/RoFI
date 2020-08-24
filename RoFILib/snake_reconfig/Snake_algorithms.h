#pragma once

#include <Configuration.h>
#include <IO.h>
#include <Generators.h>
#include <Algorithms.h>
#include "MinMaxHeap.h"
#include "Snake_structs.h"
#include <limits>
#include <queue>
#include <cmath>
#include <memory>
#include <stack>


struct SnakeEvalCompare {
public:
    bool operator()(const EvalPair& a, const EvalPair& b) const {
        return std::get<0>(a) < std::get<0>(b);
    }
};

inline double eval(const Configuration& configuration, SpaceGrid& sg) {
    sg.loadConfig(configuration);
    return sg.getDist();
}

inline std::vector<Configuration> SnakeStar(const Configuration& init, AlgorithmStat* stat = nullptr,
    int limit = 5000, double path_pref = 0.1)
{
    unsigned step = 90;
    double free_pref = 1 - path_pref;
    ConfigPred pred;
    ConfigPool pool;

    SpaceGrid grid(init.getIDs().size());
    // Already computed shortest distance from init to configuration.
    ConfigValue initDist;

    // Shortest distance from init through this configuration to goal.
    ConfigValue goalDist;

    double startDist = eval(init, grid);

    if (startDist == 0)
        return {init};

    MinMaxHeap<EvalPair, SnakeEvalCompare> queue(limit);

    const Configuration* pointer = pool.insert(init);
    const Configuration* bestConfig = pointer;
    unsigned bestScore = startDist;
    double worstDist = startDist;

    initDist[pointer] = 0;
    goalDist[pointer] = startDist;
    pred[pointer] = pointer;
    int maxQSize = 0;
    int i = 0;
    queue.push( {goalDist[pointer], pointer} );


    while (!queue.empty() && i++ < limit) {
        maxQSize = std::max(maxQSize, queue.size());
        const auto [d, current] = queue.popMin();
        double currDist = initDist[current];

        std::vector<Configuration> nextCfgs;
        simpleNext(*current, nextCfgs, step);

        for (const auto& next : nextCfgs) {
            const Configuration* pointerNext;
            double newEval = eval(next, grid);
            double newDist = path_pref * (currDist + 1) + free_pref * newEval;
            bool update = false;

            if (newEval != 0 && limit <= queue.size() + i) {
                if (newDist > worstDist)
                    continue;
                if (!queue.empty()) {
                    queue.popMax();
                    const auto [newWorstD, _worstConfig] = queue.max();
                    worstDist = newWorstD;
                }
            }

            if (newDist > worstDist)
                worstDist = newDist;

            if (!pool.has(next)) {
                pointerNext = pool.insert(next);
                initDist[pointerNext] = currDist + 1;
                update = true;
            } else {
                pointerNext = pool.get(next);
            }

            if (newEval < bestScore) {
                bestScore = newEval;
                bestConfig = pointerNext;
            }

            if ((currDist + 1 < initDist[pointerNext]) || update) {
                initDist[pointerNext] = currDist + 1;
                goalDist[pointerNext] = newDist;
                pred[pointerNext] = current;
                queue.push({newDist, pointerNext});
            }

            if (newEval == 0) {
                auto path = createPath(pred, pointerNext);
                if (stat != nullptr) {
                    stat->pathLength = path.size();
                    stat->queueSize = maxQSize;
                    stat->seenCfgs = pool.size();
                }
                return path;
            }
        }
    }
    auto path = createPath(pred, bestConfig);
    if (stat != nullptr) {
        stat->pathLength = path.size();
        stat->queueSize = maxQSize;
        stat->seenCfgs = pool.size();
    }

    return path;
}

ID closestMass(const Configuration& init) {
    Vector mass = init.massCenter();
    ID bestID = 0;
    double bestDist = std::numeric_limits<double>::max();
    for (const auto& [id, ms] : init.getMatrices()) {
        for (const auto& matrix : ms) {
            double currDist = sqDistVM(matrix, mass);
            if (currDist < bestDist) {
                bestDist = currDist;
                bestID = id;
            }
        }
    }
    return bestID;
}

class MakeStar {
public:
    MakeStar(const Configuration& init, ID root)
    : mass(init.massCenter())
    , config(init)
    , dists() {
        for (const auto& [id, ms] : init.getMatrices()) {
            double currDist = 0;
            for (unsigned side = 0; side < 2; ++side)
                currDist += sqDistVM(ms[side], mass);

            dists.emplace(id, currDist);
        }
    }

    std::vector<Edge> operator()(std::stack<ID>& dfs_stack, std::unordered_set<ID>& seen, ID curr) {
        std::vector<Edge> nEdges = config.getEdges(curr, seen);
        std::sort(nEdges.begin(), nEdges.end(), [&](const Edge& a, const Edge& b){
            return dists[a.id2()] < dists[b.id2()];
        });
        for (const auto& e : nEdges)
            dfs_stack.push(e.id2());

        return nEdges;
    }

private:
    const Vector mass;
    const Configuration& config;
    std::unordered_map<ID, double> dists;
};


using chooseRootFunc = ID(const Configuration&);

template<typename Next>
inline Configuration treefy(const Configuration& init, chooseRootFunc chooseRoot = closestMass) {
    ID root = chooseRoot(init);
    Configuration treed = init;
    treed.clearEdges();
    treed.setFixed(root, A, identity); // maybe change the choose root to return shoe as well

    std::unordered_set<ID> seen{};
    std::stack<ID> dfs_stack{};

    Next oracle(init, root);

    dfs_stack.push(root);

    while(!dfs_stack.empty()) {
        ID curr = dfs_stack.top();
        dfs_stack.pop();
        if (seen.find(curr) != seen.end())
            continue;

        seen.insert(curr);
        std::vector<Edge> edges = oracle(dfs_stack, seen, curr);
        for (const auto& e : edges)
            treed.addEdge(e);
    }

    return treed;
}

inline std::vector<Configuration> paralyzedAStar(const Configuration& init, const Configuration& goal,
    EvalFunction& eval = Eval::trivial, AlgorithmStat* stat = nullptr, std::unordered_set<ID> allowed_indices = {})
{
    ConfigPred pred;
    ConfigPool pool;
    unsigned step = 90;

    // Already computed shortest distance from init to configuration.
    ConfigValue initDist;

    // Shortest distance from init through this configuration to goal.
    ConfigValue goalDist;

    if (init == goal)
        return {init};

    std::priority_queue<EvalPair, std::vector<EvalPair>, SnakeEvalCompare> queue;

    const Configuration* pointer = pool.insert(init);
    initDist[pointer] = 0;
    goalDist[pointer] = eval(init, goal);
    pred[pointer] = pointer;
    unsigned long maxQSize = 0;

    queue.push( {goalDist[pointer], pointer} );

    while (!queue.empty()) {
        maxQSize = std::max(maxQSize, queue.size());
        const auto [d, current] = queue.top();
        double currDist = initDist[current];
        queue.pop();

        std::vector<Configuration> nextCfgs;
        paralyzedNext(*current, nextCfgs, step, allowed_indices);
        for (const auto& next : nextCfgs) {
            const Configuration* pointerNext;
            double newDist = currDist + 1 + eval(next, goal);
            bool update = false;

            if (!pool.has(next)) {
                pointerNext = pool.insert(next);
                initDist[pointerNext] = currDist + 1;
                update = true;
            } else {
                pointerNext = pool.get(next);
            }

            if ((currDist + 1 < initDist[pointerNext]) || update) {
                initDist[pointerNext] = currDist + 1;
                goalDist[pointerNext] = newDist;
                pred[pointerNext] = current;
                queue.push({newDist, pointerNext});
            }

            if (next == goal) {
                auto path = createPath(pred, pointerNext);
                if (stat != nullptr) {
                    stat->pathLength = path.size();
                    stat->queueSize = maxQSize;
                    stat->seenCfgs = pool.size();
                }
                return path;
            }
        }
    }
    if (stat != nullptr) {
        stat->pathLength = 0;
        stat->queueSize = maxQSize;
        stat->seenCfgs = pool.size();
    }
    return {};
}

bool isSnake(const Configuration& config) {
    return false;
}

double distFromConn(const Configuration& config, const Edge& connection) {
    if (config.findEdge(connection))
        return 0;
    const auto& realPos = config.getMatrices().at(connection.id2()).at(connection.side2());
    auto wantedPos = config.computeConnectedMatrix(connection);
    return sqDistance(realPos, wantedPos);
}

void addSubtree(ID subRoot, std::unordered_set<ID>& allowed, const std::unordered_map<ID, std::array<std::optional<Edge>, 6>>& spannSucc);

std::unordered_set<ID> makeAllowed(const Configuration& init, ID subroot1, ID subroot2) {
    std::unordered_set<ID> allowed;
    const auto& spannSucc = init.getSpanningSucc();
    for (ID currId : {subroot1, subroot2}) {
        addSubtree(currId, allowed, spannSucc);
    }
    return allowed;
}

std::vector<Configuration> connectArm(const Configuration& init, const Edge& connection, ID subroot1, ID subroot2, AlgorithmStat* stat = nullptr) {
    // TODO: ADD MAKESPACE!!!! 
    // Edge connection is from end of arm to end of arm
    unsigned step = 90;
    double path_pref = 0.1;
    double free_pref = 1 - path_pref;
    ConfigPred pred;
    ConfigPool pool;

    // Already computed shortest distance from init to configuration.
    ConfigValue initDist;

    // Shortest distance from init through this configuration to goal.
    ConfigValue goalDist;

    double startDist = distFromConn(init, connection);
    double worstDist = startDist;

    if (startDist == 0)
        return {init};

    auto limit = init.getModules().size() * 100;
    MinMaxHeap<EvalPair, SnakeEvalCompare> queue(limit);

    const Configuration* pointer = pool.insert(init);
    initDist[pointer] = 0;
    goalDist[pointer] = startDist;
    pred[pointer] = pointer;
    int maxQSize = 0;

    queue.push( {goalDist[pointer], pointer} );

    std::unordered_set<ID> allowed = makeAllowed(init, subroot1, subroot2);
    int i = 0;
    while (!queue.empty()) {
        maxQSize = std::max(maxQSize, queue.size());
        const auto [d, current] = queue.popMin();
        double currDist = initDist[current];
        if (++i > 100) {
            std::cout << IO::toString(*current) << std::endl;
            i = 0;
        }

        std::vector<Configuration> nextCfgs;
        biParalyzedOnlyRotNext(*current, nextCfgs, step, allowed);

        for (const auto& next : nextCfgs) {
            const Configuration* pointerNext;
            double newEval = distFromConn(next, connection);
            double newDist = path_pref * (currDist + 1) + free_pref * newEval;
            bool update = false;

            if (newEval != 0 && queue.full()) {
                if (newDist > worstDist)
                    continue;
                if (!queue.empty()) {
                    queue.popMax();
                    const auto [newWorstD, _worstConfig] = queue.max();
                    worstDist = newWorstD;
                }
            }

            if (newDist > worstDist)
                worstDist = newDist;

            if (!pool.has(next)) {
                pointerNext = pool.insert(next);
                initDist[pointerNext] = currDist + 1;
                update = true;
            } else {
                pointerNext = pool.get(next);
            }

            if ((currDist + 1 < initDist[pointerNext]) || update) {
                initDist[pointerNext] = currDist + 1;
                goalDist[pointerNext] = newDist;
                pred[pointerNext] = current;
                queue.push({newDist, pointerNext});
            }

            if (newEval == 0) {
                auto path = createPath(pred, pointerNext);
                if (stat != nullptr) {
                    stat->pathLength = path.size();
                    stat->queueSize = maxQSize;
                    stat->seenCfgs = pool.size();
                }
                return path;
            }
        }
    }
    if (stat != nullptr) {
        stat->pathLength = 0;
        stat->queueSize = maxQSize;
        stat->seenCfgs = pool.size();
    }
    return {};
}

double spaceEval(const Configuration& config, const std::unordered_set<ID>& isolate) {
    double dist = 0;
    const auto& matrixMap = config.getMatrices();
    for (const auto& [id, mArray] : matrixMap) {
        if (isolate.find(id) != isolate.end())
            continue;
        for (auto isolatedId : isolate) {
            const auto& isolatedMArray = matrixMap.at(isolatedId);
            for (const auto& m1 : mArray) {
                for (const auto& m2 : isolatedMArray) {
                    dist += 1 / centerSqDistance(m1, m2);
                }
            }
        }
    }
    return dist;
}

std::vector<Configuration> makeSpace(const Configuration& init, const std::unordered_set<ID>& isolate, AlgorithmStat* stat = nullptr) {
    unsigned step = 90;
    unsigned limit = 10 * init.getModules().size();
    double path_pref = 0.1;
    double free_pref = 1 - path_pref;
    ConfigPred pred;
    ConfigPool pool;

    // Already computed shortest distance from init to configuration.
    ConfigValue initDist;

    // Shortest distance from init through this configuration to goal.
    ConfigValue goalDist;

    double startDist = spaceEval(init, isolate);

    MinMaxHeap<EvalPair, SnakeEvalCompare> queue(limit);

    const Configuration* pointer = pool.insert(init);
    const Configuration* bestConfig = pointer;
    double bestScore = startDist;
    double worstDist = startDist;

    initDist[pointer] = 0;
    goalDist[pointer] = startDist;
    pred[pointer] = pointer;
    int maxQSize = 0;
    int i = 0;
    queue.push( {goalDist[pointer], pointer} );


    while (!queue.empty() && i++ < limit) {
        maxQSize = std::max(maxQSize, queue.size());
        const auto [d, current] = queue.popMin();
        double currDist = initDist[current];

        std::vector<Configuration> nextCfgs;
        bisimpleNext(*current, nextCfgs, step);

        for (const auto& next : nextCfgs) {
            const Configuration* pointerNext;
            double newEval = spaceEval(next, isolate);
            double newDist = path_pref * (currDist + 1) + free_pref * newEval;
            bool update = false;

            if (limit <= queue.size() + i) {
                if (newDist > worstDist)
                    continue;
                if (!queue.empty()) {
                    queue.popMax();
                    const auto [newWorstD, _worstConfig] = queue.max();
                    worstDist = newWorstD;
                }
            }

            if (newDist > worstDist)
                worstDist = newDist;

            if (!pool.has(next)) {
                pointerNext = pool.insert(next);
                initDist[pointerNext] = currDist + 1;
                update = true;
            } else {
                pointerNext = pool.get(next);
            }

            if (newEval < bestScore) {
                bestScore = newEval;
                bestConfig = pointerNext;
            }

            if ((currDist + 1 < initDist[pointerNext]) || update) {
                initDist[pointerNext] = currDist + 1;
                goalDist[pointerNext] = newDist;
                pred[pointerNext] = current;
                queue.push({newDist, pointerNext});
            }
        }
    }
    auto path = createPath(pred, bestConfig);
    if (stat != nullptr) {
        stat->pathLength = path.size();
        stat->queueSize = maxQSize;
        stat->seenCfgs = pool.size();
    }

    return path;
}

void addSubtree(ID subRoot, std::unordered_set<ID>& allowed, const std::unordered_map<ID, std::array<std::optional<Edge>, 6>>& spannSucc) {
    std::queue<ID> toAdd;
    toAdd.push(subRoot);
    while (!toAdd.empty()) {
        auto currId = toAdd.front();
        toAdd.pop();
        allowed.insert(currId);
        for (const auto& optEdge : spannSucc.at(currId)) {
            if (!optEdge.has_value())
                continue;
            toAdd.push(optEdge.value().id2());
        }
    }
}

bool isFixed(const Configuration& fixed, const std::unordered_set<ID>& toFix) {
    const auto& spannTree = fixed.getSpanningSucc();
    unsigned zCon = 0;
    for (const auto& id : toFix) {
        for (const auto& optEdge : spannTree.at(id)) {
            if (!optEdge.has_value())
                continue;
            const auto& edge = optEdge.value();
            if (toFix.find(edge.id2()) == toFix.end())
                continue;
            if (edge.dock1() != ZMinus || edge.dock2() != ZMinus)
                continue;
            ++zCon;
        }
    }
    return zCon > 1;
}

std::optional<std::vector<Configuration>> boundedLeafDfs(const Configuration& curr, const std::unordered_set<ID>& allowed, std::unordered_set<Configuration, ConfigurationHash>& seen, unsigned limit) {
    if (limit <= 0)
        return {};
    seen.insert(curr);
    std::vector<Configuration> nextCfgs;
    unsigned step = 90;
    paralyzedNext(curr, nextCfgs, step, allowed);
    for (const auto& next : nextCfgs) {
        if (seen.find(next) != seen.end())
            continue;
        if (isFixed(next, allowed))
            return std::vector<Configuration>{next};
        seen.insert(next);
        auto res = boundedLeafDfs(next, allowed, seen, limit - 1);
        if (res.has_value()) {
            res.value().push_back(curr);
            return res.value();
        }
    }
    return {};
}

std::vector<Configuration> leafDfs(const Configuration& init, const std::unordered_set<ID>& allowed) {
    unsigned limit = 1;
    std::unordered_set<Configuration, ConfigurationHash> seen;
    while (true) {
        std::cout << limit << std::endl;
        auto res = boundedLeafDfs(init, allowed, seen, limit);
        if (res.has_value())
            return res.value();
        ++limit;
        seen.clear();
    }
    return {};
}

double moduleDist(ID id1, ID id2, const std::unordered_map<ID, std::array<Matrix, 2>>& matrices) {
    return moduleDistance(matrices.at(id1)[0], matrices.at(id1)[1], matrices.at(id2)[0], matrices.at(id2)[1]);
}

bool areLeafsFree(ID id1, ID id2, ID id3, const Configuration& config) {
    std::unordered_set<ID> toFix = {id1, id2, id3};
    for (auto id : toFix) {
        for (const auto& optEdge : config.getSpanningSucc().at(id)) {
            if (!optEdge.has_value())
                continue;
            const auto& edge = optEdge.value();
            if (toFix.find(edge.id2()) != toFix.end())
                continue;
            return false;
        }
    }
    return true;
}


std::unordered_set<ID> chooseLeafsToMove(const std::unordered_set<ID>& leafs, const Configuration& config) {
    const auto& matrices = config.getMatrices();
    if (leafs.size() < 4)
        return leafs;
    auto best1 = leafs.begin();
    auto best2 = leafs.begin();
    auto best3 = leafs.begin();
    double bestScore = std::numeric_limits<double>::max();
    for (auto it1 = leafs.begin(); it1 != leafs.end(); ++it1) {
        for (auto it2 = it1; it2 != leafs.end(); ++it2) {
            if (it1 == it2)
                continue;
            auto dist12 = moduleDist(*it1, *it2, matrices);
            if (dist12 >= bestScore)
                continue;
            for (auto it3 = it2; it3 != leafs.end(); ++it3) {
                if (it2 == it3)
                    continue;
                auto addDist = dist12 + moduleDist(*it1, *it3, matrices);
                if (addDist >= bestScore)
                    continue;
                addDist += moduleDist(*it2, *it3, matrices);
                if (addDist >= bestScore)
                    continue;
                if (!areLeafsFree(*it1, *it2, *it3, config))
                    continue;
                best1 = it1;
                best2 = it2;
                best3 = it3;
                bestScore = addDist;
            }
        }
    }
    return {*best1, *best2, *best3};
}

std::vector<Configuration> fixLeaf(const Configuration& init, ID toFix) {
    const auto& spannSuccCount = init.getSpanningSuccCount();
    const auto& spannSucc = init.getSpanningSucc();
    const auto& spannPred = init.getSpanningPred();
    if (spannSuccCount.at(toFix) != 0)
        return {};

    if (!spannPred.at(toFix).has_value())
        return {};

    auto pred = spannPred.at(toFix).value().first;
    std::unordered_set<ID> allowed;
    if (spannSuccCount.at(pred) > 1) {
        addSubtree(pred, allowed, spannSucc);
    } else if (spannPred.at(pred).has_value()) {
        addSubtree(spannPred.at(pred).value().first, allowed, spannSucc);
    } else {
        return {};
    }

    auto trueAllowed = chooseLeafsToMove(allowed, init);
    auto res = leafDfs(init, trueAllowed);
    std::vector<Configuration> revRes;
    for (auto i = res.size(); i > 0; --i) {
        revRes.push_back(res[i-1]);
    }
    return revRes;
}

void findLeafs(const Configuration& config, std::unordered_map<ID, ShoeId>& leafsBlack, std::unordered_map<ID, ShoeId>& leafsWhite) {
    std::queue<std::tuple<ID, ShoeId, bool>> bag;
    bag.emplace(config.getFixedId(), config.getFixedSide(), true);
    const auto& spannSucc = config.getSpanningSucc();
    while(!bag.empty()) {
        auto [currId, currShoe, isWhite] = bag.front();
        auto otherShoe = currShoe == A ? B : A;
        bag.pop();
        bool isLeaf = true;
        for (const auto& optEdge : spannSucc.at(currId)) {
            if (!optEdge.has_value())
                continue;
            isLeaf = false;
            bag.emplace(optEdge.value().id2(), optEdge.value().side2(), !isWhite);
        }
        if (!isLeaf)
            continue;
        if (isWhite) {
            leafsWhite.emplace(currId, otherShoe);
        } else {
            leafsBlack.emplace(currId, otherShoe);
        }
    }
}

void computeSubtreeSizes(const Configuration& config, std::unordered_map<ID, unsigned>& subtreeSizes) {
    std::vector<ID> bag;
    auto moduleCount = config.getModules().size();
    const auto& spannSucc = config.getSpanningSucc();
    bag.reserve(moduleCount);
    unsigned currIndex = 0;
    bag.emplace_back(config.getFixedId());
    while (bag.size() < moduleCount) {
        auto currId = bag[currIndex];
        for (const auto& optEdge : spannSucc.at(currId)) {
            if (!optEdge.has_value())
                continue;
            bag.emplace_back(optEdge.value().id2());
        }
        ++currIndex;
    }
    for (int i = moduleCount - 1; i >= 0; --i) {
        auto currId = bag[i];
        subtreeSizes[currId] = 1;
        for (const auto& optEdge : spannSucc.at(currId)) {
            if (!optEdge.has_value())
                continue;
            subtreeSizes[currId] += subtreeSizes[optEdge.value().id2()];
        }
    }
}

std::tuple<ID, ShoeId, unsigned> computeActiveRadius(const Configuration& config, const std::unordered_map<ID, unsigned>& subtreeSizes, ID id, ShoeId shoe) {
    const auto& spannPred = config.getSpanningPred();
    if (!spannPred.at(id).has_value())
        return {id, shoe == A ? B : A, 0};
    const auto& spannSucc = config.getSpanningSucc();
    unsigned radius = 2;
    unsigned currSize = 1;
    auto [currId, currShoe] = spannPred.at(id).value();
    auto prevId = id;
    auto prevShoe = shoe;
    while (spannPred.at(currId).has_value()) {
        prevId = currId;
        prevShoe = currShoe;
        std::tie(currId, currShoe) = spannPred.at(currId).value();

        if (subtreeSizes.at(currId) > 2 * currSize + 1) {
            for (const auto& optEdge : spannSucc.at(currId)) {
                if (!optEdge.has_value())
                    continue;
                if (optEdge.value().id2() != prevId)
                    continue;
                return {prevId, optEdge.value().side2() == A ? B : A, radius};
            }
            throw 42;
        }

        if (!spannPred.at(currId).has_value()) {
            radius += 2;
        } else {
            const auto& [predId, predShoe] = spannPred.at(currId).value();
            for (const auto& optEdge : spannSucc.at(predId)) {
                if (!optEdge.has_value())
                    continue;
                if (optEdge.value().id2() != currId)
                    continue;
                if (optEdge.value().side2() == currShoe) {
                    radius += 1;
                } else {
                    radius += 2;
                }
            }
        }

        currSize = subtreeSizes.at(currId);
    }

    return {currId, spannPred.at(prevId).value().second == A ? B : A, radius};
}

void computeActiveRadiuses(const Configuration& config, const std::unordered_map<ID, unsigned>& subtreeSizes, const std::unordered_map<ID, ShoeId>& ids, std::unordered_map<ID, std::tuple<ID, ShoeId, unsigned>>& radiuses) {
    for (const auto& [id, shoe] : ids) {
        radiuses[id] = computeActiveRadius(config, subtreeSizes, id, shoe);
    }
}

std::vector<Configuration> treeToSnake(const Configuration& init, AlgorithmStat* stat = nullptr) {
    // držet si konce v prioritní frontě, když to neuspěje, tak popni
    //      podívej se na top fronty + najdi nejbližší jiný konec s opačnou polaritou (který je dostatečně blízko)
    //          vytvoř prostor na zavolání crippled-astar
    //          zavolej vytvoření prostoru
    //          zavolej crippled-astar s magnetem mezi konci
    //          pokud selže, najdi vyzkoušej najít jiný konec
    // jsi had, tak ok. Jinak jsme smutní.
    std::unordered_set<ID> seenBlack;
    std::unordered_set<ID> seenWhite;
    std::unordered_map<ID, ShoeId> leafsBlack;
    std::unordered_map<ID, ShoeId> leafsWhite;
    std::vector<std::pair<ID, ShoeId>> vecLeafsBlack;
    std::vector<std::pair<ID, ShoeId>> vecLeafsWhite;


    findLeafs(init, leafsBlack, leafsWhite);

    std::unordered_map<ID, unsigned> subtreeSizes;
    std::unordered_map<ID, std::tuple<ID, ShoeId, unsigned>> activeRadiuses;

    std::vector<Configuration> path = {init};

    std::unordered_map<ID, double> whiteDists;
    std::vector<Configuration> res;

    while (true) {
        res.clear();
        auto& config = path.back();
        const auto& matrices = config.getMatrices();

        computeSubtreeSizes(config, subtreeSizes);
        computeActiveRadiuses(config, subtreeSizes, leafsBlack, activeRadiuses);

        vecLeafsBlack.clear();
        vecLeafsBlack.reserve(leafsBlack.size());
        for (const auto& [id, shoe] : leafsBlack) {
            vecLeafsBlack.emplace_back(id, shoe);
        }
        std::sort(vecLeafsBlack.begin(), vecLeafsBlack.end(), [&](const std::pair<ID, ShoeId>& a, const std::pair<ID, ShoeId>& b) {
            return std::get<2>(activeRadiuses.at(a.first)) > std::get<2>(activeRadiuses.at(b.first));
        });

        for (const auto& [id, shoe] : vecLeafsBlack) {
            vecLeafsWhite.clear();
            vecLeafsWhite.reserve(leafsWhite.size());
            for (const auto& [w_id, w_shoe] : leafsWhite) {
                vecLeafsWhite.emplace_back(w_id, w_shoe);
                whiteDists[id] = centerSqDistance(matrices.at(id)[shoe], matrices.at(w_id)[w_shoe]);
            }
            std::sort(vecLeafsWhite.begin(), vecLeafsWhite.end(), [&](const std::pair<ID, ShoeId>& a, const std::pair<ID, ShoeId>& b) {
                return whiteDists.at(a.first) < whiteDists.at(b.first);
            });

            const auto& [subRoot, subRootSide, radius] = activeRadiuses.at(id);
            for (const auto& [w_id, w_shoe] : vecLeafsWhite) {
                auto [w_subRoot, w_subRootSide, w_radius] = computeActiveRadius(config, subtreeSizes, w_id, w_shoe);
                unsigned rootDist = newyorkCenterDistance(matrices.at(subRoot)[subRootSide], matrices.at(w_subRoot)[w_subRootSide]);
                if (rootDist > radius + w_radius) {
                    continue;
                }
                Edge desiredConn(id, shoe, ConnectorId::ZMinus, Orientation::North, ConnectorId::ZMinus, w_shoe, w_id);
                auto res = connectArm(config, desiredConn, subRoot, w_subRoot);
                if (!res.empty()) {
                    // TODO: ADD changing sets and removing unwanted edge
                    break;
                }
            }
            if (!res.empty())
                break;
        }
        if (res.empty())
            return {};

        for (const auto& resConf : res)
            path.emplace_back(resConf);

    }
    return {};
}

std::vector<Configuration> reconfigToSnake(const Configuration& init, AlgorithmStat* stat = nullptr) {
    std::vector<Configuration> path = SnakeStar(init, stat);
    if (path.empty())
        return path;
    path.push_back(treefy<MakeStar>(path.back()));
    auto toSnake = treeToSnake(path.back(), stat);
    path.reserve(path.size() + toSnake.size());
    for (const auto& config : toSnake) {
        path.push_back(config);
    }
    return path;
}