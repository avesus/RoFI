//
// Created by xmichel on 4/12/19.
//

#include "DistributedModule.h"

DistributedModule::DistributedModule(unsigned int id, std::ifstream &inStream, std::ifstream &trgStream)
        : currModule(id), trgModule(id) {
    if (!inStream.is_open() || !trgStream.is_open()) {
        return;
    }

    DistributedReader::readDistributedModule(inStream, currModule);
    DistributedReader::readDistributedModule(trgStream, trgModule);
}

std::string DistributedModule::printCurrModule(int step) const {
    return printModule(currModule, step);
}

std::string DistributedModule::printTrgModule(int step) const {
    return printModule(trgModule, step);
}

std::string DistributedModule::printCurrConfiguration(int step) const {
    return DistributedPrinter::toString(currConfiguration, step);
}

std::string DistributedModule::printTrgConfiguration(int step) const {
    return DistributedPrinter::toString(trgConfiguration, step);
}

void DistributedModule::reconfigurate() {
    shareCurrConfigurations();
    shareTrgConfigurations();

    auto generatePath = AStar(currConfiguration, trgConfiguration, 90, 1, Eval::actionDiff);
    std::vector<Action> generatedActions = createDiffs(generatePath);
    optimizePath(generatedActions);

    for (unsigned int i = 0; i < generatedActions.size(); i++) {
        Action diff = generatedActions.at(i);
        executeDiff(diff, i + 1);
        MPI_Barrier(MPI_COMM_WORLD);
    }
}

EdgeList DistributedModule::createEdgeListFromEdges(const std::vector<Edge> &edges, unsigned int id) const {
    EdgeList arrayEdges;

    for (const Edge &edge : edges) {
        if (edge.id1() == id) {
            arrayEdges[edge.side1() * 3 + edge.dock1()] = edge;
        } else {
            arrayEdges[edge.side2() * 3 + edge.dock2()] = edge;
        }
    }

    return arrayEdges;
}

void DistributedModule::shareConfigurations(const DistributedModuleProperties &module, Configuration &configuration) {
    int worldSize;
    MPI_Comm_size(MPI_COMM_WORLD, &worldSize);

    DistributedModuleProperties moduleToSend[worldSize];
    for (int i = 0; i < worldSize; i++) {
        moduleToSend[i] = module;
    }

    int sizeOfModule = sizeof(DistributedModuleProperties);
    auto moduleToSendChar = reinterpret_cast<const char *>(moduleToSend);
    char moduleToRecvChar[sizeOfModule * worldSize];

    MPI_Alltoall(moduleToSendChar, sizeOfModule, MPI_CHAR, moduleToRecvChar, sizeOfModule, MPI_CHAR, MPI_COMM_WORLD);

    auto moduleToRecv = reinterpret_cast<DistributedModuleProperties *>(moduleToRecvChar);
    for (int i = 0; i < worldSize; i++) {
        DistributedModuleProperties &module1 = moduleToRecv[i];
        configuration.addModule(module1.getJoint(Joint::Alpha), module1.getJoint(Joint::Beta), module1.getJoint(Joint::Gamma), module1.getId());
    }

    for (int i = 0; i < worldSize; i++) {
        DistributedModuleProperties &module1 = moduleToRecv[i];
        for (auto optEdge : module1.getEdges()) {
            if (!optEdge.has_value()) {
                continue;
            }

            Edge edge = optEdge.value();
            ID id1 = edge.id1();
            ID id2 = edge.id2();

            if ((module1.getId() == id1 && id1 < id2) || (module1.getId() == id2 && id1 > id2)) {
                configuration.addEdge(edge);
            }
        }
    }
}

std::string DistributedModule::printModule(const DistributedModuleProperties &module, int step) const {
    std::stringstream out;
    out << DistributedPrinter::toString(module, step);

    for (auto &optEdge : module.getEdges()) {
        if (!optEdge.has_value()) {
            continue;
        }

        Edge &edge = optEdge.value();
        int id1 = edge.id1();
        int id2 = edge.id2();

        if ((id1 < id2 && module.getId() == id1) || (id1 > id2 && module.getId() == id2)) {
            out << DistributedPrinter::toString(edge, step);
        }
    }

    return out.str();
}

void DistributedModule::executeDiff(const Action &action, int step) {
    for (const auto &rotation : action.rotations()) {
        if (currModule.getId() == rotation.id()) {
            currModule.execute(rotation);
            std::cout << DistributedPrinter::toString(rotation, step);
        }
    }

    for (const auto &reconnection : action.reconnections()) {
        if (currModule.getId() == reconnection.edge().id1()) {
            currModule.execute(reconnection);
            std::cout << DistributedPrinter::toString(reconnection, step);
        } else if (currModule.getId() == reconnection.edge().id2()) {
            currModule.execute(reconnection);
        }
    }
}

std::vector<Action> DistributedModule::createDiffs(const std::vector<Configuration> &path) const {
    std::vector<Action> rv;
    for (unsigned int i = 1; i < path.size(); i++) {
        Action diff = path.at(i - 1).diff(path.at(i));
        rv.push_back(diff);
    }

    return rv;
}

void DistributedModule::optimizePath(std::vector<Action> &path) const {
    joinActions(path);
    bool doMoreOpt = true;

    while (doMoreOpt) {
        swapActions(path);
        doMoreOpt = joinActions(path);
    }
}

bool DistributedModule::joinActions(std::vector<Action> &path) const {
    if (path.empty()) {
        return false;
    }

    bool rv = false;
    Configuration curr = currConfiguration;
    std::vector<Action> newActions;
    newActions.push_back(path.at(0));
    unsigned long currIndex = 0;

    for (unsigned long i = 1; i < path.size(); i++) {
        Configuration correct = curr;
        correct.execute(newActions.at(currIndex));
        correct.execute(path.at(i));

        std::vector<Action::Rotate> rotate = newActions.at(currIndex).rotations();
        std::vector<Action::Reconnect> reconnect = newActions.at(currIndex).reconnections();
        std::vector<Action::Rotate> rotate2 = path.at(i).rotations();
        std::vector<Action::Reconnect> reconnect2 = path.at(i).reconnections();
        rotate.insert(rotate.end(), rotate2.begin(), rotate2.end());
        reconnect.insert(reconnect.end(), reconnect2.begin(), reconnect2.end());

        auto optionalCfg = curr.executeIfValid({rotate, reconnect});

        if (!optionalCfg.has_value() || optionalCfg.value() != correct) {
            curr.execute(newActions.at(currIndex));
            newActions.push_back(path.at(i));
            currIndex++;
            continue;
        }

        rv = true;
        newActions[currIndex] = {rotate, reconnect};
    }

    path.clear();
    path = newActions;
    return rv;
}

void DistributedModule::swapActions(std::vector<Action> &path) const {
    Configuration beforeFst = currConfiguration;

    for (unsigned long i = 1; i < path.size(); i++) {
        Configuration correctOrder = beforeFst;

        correctOrder.execute(path.at(i - 1));

        std::optional<Configuration> swapOrder = beforeFst.executeIfValid(path.at(i));
        if (!swapOrder.has_value()) {
            beforeFst = correctOrder;
            continue;
        }

        swapOrder = swapOrder.value().executeIfValid(path.at(i - 1));
        if (!swapOrder.has_value()) {
            beforeFst = correctOrder;
            continue;
        }

        correctOrder.execute(path.at(i));
        if (correctOrder == swapOrder.value()) {
            std::swap(path[i], path[i - 1]);
        }

        beforeFst.execute(path.at(i - 1));
    }

}

void DistributedModule::reconfigurate2() {
    if (currModule.getId() == 0) {
        matrixA = identity;
        matrixAInit = true;
    }

    shareCoordinates();
    auto coorA = center(matrixA);
    std::cout << "A coor " << currModule.getId() << " " << coorA.at(0) << " " << coorA.at(1) << " " << coorA(2) << std::endl;
    auto coorB = center(matrixB);
    std::cout << "B coor " << currModule.getId() << " " << coorB.at(0) << " " << coorB.at(1) << " " << coorB(2) << std::endl;
}

void DistributedModule::shareCoordinates() {
    std::vector<Edge> edges;
    for (auto &edge : currModule.getEdges()) {
        if (edge.has_value()) {
            edges.push_back(edge.value());
        }
    }
    std::sort(edges.begin(), edges.end(), compareEdge);

    std::vector<DistributedModuleProperties> neighbours;
    for (auto &edge : edges) {
        MPI_Status status;
        auto currModuleToSend = reinterpret_cast<const char *>(&currModule);
        char moduleToRecv[sizeof(DistributedModuleProperties)];
        MPI_Sendrecv(currModuleToSend, sizeof(DistributedModuleProperties), MPI_CHAR, edge.id2(), 0
                , moduleToRecv, sizeof(DistributedModuleProperties), MPI_CHAR, edge.id2(), 0
                , MPI_COMM_WORLD, &status);
        neighbours.push_back(*reinterpret_cast<DistributedModuleProperties *>(moduleToRecv));
    }

    Configuration cfg;
    cfg.addModule(currModule.getJoint(Joint::Alpha), currModule.getJoint(Joint::Beta), currModule.getJoint(Joint::Gamma)
            , currModule.getId());

    for (auto &module : neighbours) {
        cfg.addModule(module.getJoint(Joint::Alpha), module.getJoint(Joint::Beta), module.getJoint(Joint::Gamma)
                , module.getId());
    }

    const ModuleMap &modules = cfg.getModules();
    for (auto &module : neighbours) {
        for (auto optEdge : module.getEdges()) {
            if (!optEdge.has_value()) {
                continue;
            }

            Edge edge = optEdge.value();
            ID id1 = edge.id1();
            ID id2 = edge.id2();

            if (module.getId() == std::min(id1, id2) && modules.find(std::max(id1, id2)) != modules.end()) {
                cfg.addEdge(edge);
            }
        }
    }

    for (auto optEdge : currModule.getEdges()) {
        if (!optEdge.has_value()) {
            continue;
        }

        Edge edge = optEdge.value();
        ID id1 = edge.id1();
        ID id2 = edge.id2();

        if (currModule.getId() == std::min(id1, id2) && modules.find(std::max(id1, id2)) != modules.end()) {
            cfg.addEdge(edge);
        }
    }

    std::vector<ID> idsHasMatrix;
    while (!matrixAInit && !matrixBInit) {
        for (const auto &edge : edges) {
            MPI_Status status;
            bool toSend = false;
            bool toRecv;
            MPI_Sendrecv(&toSend, 1, MPI_CXX_BOOL, edge.id2(), 1, &toRecv, 1, MPI_CXX_BOOL, edge.id2(), 1
                    , MPI_COMM_WORLD, &status);

            if (toRecv) {
                char matrixToRecv[sizeof(Matrix)];
                MPI_Recv(matrixToRecv, sizeof(Matrix), MPI_CHAR, edge.id2(), 2, MPI_COMM_WORLD, &status);
                auto matrix = reinterpret_cast<Matrix *>(matrixToRecv);

                if (edge.side1() == A) {
                    matrixA = *matrix;
                    matrixAInit = true;
                } else {
                    matrixB = *matrix;
                    matrixBInit = true;
                }

                idsHasMatrix.push_back(edge.id2());
            }
        }
    }

    if (matrixAInit) {
        cfg.setFixed(currModule.getId(), A, matrixA);
    } else {
        cfg.setFixed(currModule.getId(), B, matrixB);
    }

    cfg.computeMatrices();
    const MatrixMap matrices = cfg.getMatrices();

    if (matrixAInit) {
        matrixB = matrices.at(currModule.getId()).at(1);
        matrixBInit = true;
    } else {
        matrixA = matrices.at(currModule.getId()).at(0);
        matrixAInit = true;
    }

    for (const auto &edge : edges) {
        bool hasMatrix = false;
        for (ID &id : idsHasMatrix) {
            if (edge.id2() == id) {
                hasMatrix = true;
                break;
            }
        }

        if (hasMatrix) {
            continue;
        }

        MPI_Status status;
        bool toSend = true;
        bool toRecv;
        MPI_Sendrecv(&toSend, 1, MPI_CXX_BOOL, edge.id2(), 1, &toRecv, 1, MPI_CXX_BOOL, edge.id2(), 1
                , MPI_COMM_WORLD, &status);

        if (!toRecv) {
            Matrix matrix = matrices.at(edge.id2()).at(edge.side2());
            auto matrixToSend = reinterpret_cast<const char *>(&matrix);
            MPI_Send(matrixToSend, sizeof(Matrix), MPI_CHAR, edge.id2(), 2, MPI_COMM_WORLD);
        }
    }
}

void DistributedModule::tryConnect(const Edge &edge, int step) {
    int id2 = edge.id2();
    MPI_Bcast(&id2, 1, MPI_INT, currModule.getId(), MPI_COMM_WORLD);

    Matrix matrix;
    if (edge.side1() == Side::A) {
        matrix = matrixA * transformConnection(edge.dock1(), edge.ori(), edge.dock2());
    } else {
        matrix = matrixB * transformConnection(edge.dock1(), edge.ori(), edge.dock2());
    }

    std::pair<Matrix, Edge> pairToSend = {matrix, edge};
    auto pairToSendChar = reinterpret_cast<const char *>(&pairToSend);
    MPI_Send(pairToSendChar, sizeof(std::pair<Matrix, Edge>), MPI_CHAR, id2, 4, MPI_COMM_WORLD);

    MPI_Status status;
    bool canConnect;
    MPI_Recv(&canConnect, 1, MPI_CXX_BOOL, id2, 4, MPI_COMM_WORLD, &status);

    if (canConnect) {
        currModule.addEdge(edge);
        std::cout << DistributedPrinter::toString(edge, step);
    }

}

void DistributedModule::tryConnectOther(ID other) {
    int id;
    MPI_Bcast(&id, 1, MPI_INT, other, MPI_COMM_WORLD);

    if (id != currModule.getId()) {
        return;
    }

    char pairToRecvChar[sizeof(std::pair<Matrix, Edge>)];
    MPI_Status status;
    MPI_Recv(pairToRecvChar, sizeof(std::pair<Matrix, Edge>), MPI_CHAR, other, 4, MPI_COMM_WORLD, &status);
    auto [matrix, edge] = *reinterpret_cast<std::pair<Matrix, Edge> *>(pairToRecvChar);

    bool canConnect = (edge.side2() == Side::A && equals(matrixA, matrix))
            || (edge.side2() == Side::B && equals(matrixB, matrix));
    MPI_Send(&canConnect, 1, MPI_CXX_BOOL, other, 4, MPI_COMM_WORLD);

    if (canConnect) {
        currModule.addEdge(edge);
    }
}
