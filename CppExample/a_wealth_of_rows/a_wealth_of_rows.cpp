#include <chrono>
#include <cmath>
#include <cstdint>
#include <format>
#include <iostream>
#include <queue>

#include "Ers/Debugging/Debugger.h"
#include "Ers/Debugging/Profiler.h"
#include "Ers/Event/EventSignal.h"
#include "Ers/Model/ModelContainer.h"
#include "Ers/Model/ModelManager.h"
#include "Ers/SubModel/Component/RelationComponent.h"
#include "Ers/SubModel/Entity.h"
#include "Ers/SubModel/EventScheduler.h"
#include "Ers/Utility/Util.h"

#include "Ers/Model/ModelManager.h"

#include "Ers/Model/Simulator/Simulator.h"

#include "Ers/Logger.h"

namespace WealthOfRows
{
    class SubModelStatistics : public Ers::ScriptBehaviorComponent
    {
    public:
        SubModelStatistics() :
            NumberOfGeneratedEntities(0),
            NumberOfMovedEntities(0)
        {
        }

        void OnStart();

        void OnDestroy();
        void OnEntered(Ers::Entity parent, Ers::Entity newChild);
        void OnExited(Ers::Entity parent, Ers::Entity oldChild);

        uint64_t NumberOfGeneratedEntities;
        uint64_t NumberOfMovedEntities;
        std::vector<EntityID> Conveyors;

        Ers::EventSignalBase::Connection OnEnteredConnection;
        Ers::EventSignalBase::Connection OnExitedConnection;

        static const char* StatisticsEntityName;
    };

    struct SinkPropertiesComponent : public Ers::ScriptBehaviorComponent
    {

        uint64_t ReceivedToats{ 0 };
        std::vector<std::queue<EntityID>> IncomingQueues;

        bool operator==(const SinkPropertiesComponent& other) const { return this == &other; }
    };

    struct ConveyorPropertiesComponent : public Ers::DataComponent
    {

        uint64_t Capacity{ 1 };
        uint64_t MinimumTime{ 2 };
        uint64_t ChanceOfDelay{ 0 };
        uint64_t DelayTimeMin{ 1 };
        uint64_t DelayTimeMax{ 10 };
        bool AllowedToMoveOut{ false };

        uint64_t ConveyorIndex{ 0 };
        EntityID StatisticsEntity{ Ers::Entity::InvalidEntity };

        // Contains all entities currently present in this conveyor
        std::queue<EntityID> ToteQueue;

        bool operator==(const ConveyorPropertiesComponent& other) const { return this == &other; }
    };

    class ConveyorScriptBehavior : public Ers::ScriptBehaviorComponent
    {

    public:
        ConveyorScriptBehavior();

        void OnAwake() override;
        void OnDestroy() override;

        void OnStart() override;
        void CreateToteEvent();

        void OnEntered(const EntityID& newChild);
        void OnExited(const EntityID& oldChild);

    private:
        void DelayOrMove(const EntityID& primedTote);
        void MoveRequest(const EntityID& primedTote);
    };

    struct SinkContext
    {
        EntityID SinkEntity;
    };

    ConveyorScriptBehavior::ConveyorScriptBehavior()
    {
    }

    void ConveyorScriptBehavior::OnAwake()
    {
        auto& submodel = Ers::GetSubModel();

        auto properties = submodel.GetComponent<ConveyorPropertiesComponent>(ConnectedEntity);

        // Cache statisticsEntity
        properties->StatisticsEntity = submodel.FindEntity(SubModelStatistics::StatisticsEntityName);
    }

    void ConveyorScriptBehavior::OnDestroy()
    {
    }

    void ConveyorScriptBehavior::OnStart()
    {
    }

    void ConveyorScriptBehavior::CreateToteEvent()
    {
        auto& submodel = Ers::GetSubModel();
        auto properties = submodel.GetComponent<ConveyorPropertiesComponent>(ConnectedEntity);

        const EntityID toat = submodel.CreateEntity("");

        auto& sm = Ers::GetSubModel();
        auto statistics = submodel.GetComponent<SubModelStatistics>(properties->StatisticsEntity);
        statistics->NumberOfGeneratedEntities++;

        submodel.UpdateParentOnEntity(toat, ConnectedEntity);

        SimulationTime eventDelay(std::round(submodel.SampleRandomGenerator() * static_cast<double>(1'000'000)));
        Ers::ApplyModelPrecision(eventDelay);
        eventDelay /= SimulationTime(100000);

        Ers::EventScheduler::ScheduleLocalEvent(0, eventDelay, [this]() { CreateToteEvent(); });
    }

    void ConveyorScriptBehavior::OnEntered(const EntityID& newChild)
    {
        auto& submodel = Ers::GetSubModel();
        auto properties = submodel.GetComponent<ConveyorPropertiesComponent>(ConnectedEntity);

        properties->ToteQueue.emplace(newChild);

        if (properties->ConveyorIndex != 0)
        {
            // add delay
            SimulationTime timespan(properties->MinimumTime);
            Ers::ApplyModelPrecision(timespan);

            // Schedule events to advance the totes in the queue
            Ers::EventScheduler::ScheduleLocalEvent(0, timespan, [this, newChild]() { DelayOrMove(newChild); });
        }
        else
        {
            MoveRequest(newChild);
        }
    }

    void ConveyorScriptBehavior::OnExited([[maybe_unused]] const EntityID& oldChild)
    {
        auto& submodel = Ers::GetSubModel();

        auto properties = submodel.GetComponent<ConveyorPropertiesComponent>(ConnectedEntity);

        properties->ToteQueue.pop();

        // This is an implcit check for sources
        if (properties->Capacity > 1)
        {
            properties->AllowedToMoveOut = true;
        }
    }

    void ConveyorScriptBehavior::DelayOrMove(const EntityID& primedTote)
    {
        auto& submodel = Ers::GetSubModel();

        auto properties = submodel.GetComponent<ConveyorPropertiesComponent>(ConnectedEntity);

        // Add randomized delay
        if (submodel.SampleRandomGenerator() * 100.0 <= static_cast<double>(properties->ChanceOfDelay))
        {
            SimulationTime randomDelay((submodel.SampleRandomGenerator() * 100000) / 100000);

            randomDelay *= SimulationTime(properties->DelayTimeMax - properties->DelayTimeMin);

            SimulationTime delay(properties->DelayTimeMin);
            delay += randomDelay;
            Ers::ApplyModelPrecision(randomDelay);

            Ers::EventScheduler::ScheduleLocalEvent(0, delay, [this, primedTote]() { DelayOrMove(primedTote); });
            return;
        }

        properties->AllowedToMoveOut = true;

        MoveRequest(primedTote);
    }

    void ConveyorScriptBehavior::MoveRequest(const EntityID& primedTote)
    {
        auto& submodel = Ers::GetSubModel();

        auto properties = submodel.GetComponent<ConveyorPropertiesComponent>(ConnectedEntity);

        if (!properties->AllowedToMoveOut)
        {
            return;
        }

        auto simulator = submodel.GetSimulator();
        uint32_t simulatorId = simulator.GetID();

        auto statistics = submodel.GetComponent<SubModelStatistics>(properties->StatisticsEntity);

        if (statistics->Conveyors.size() - 1 == properties->ConveyorIndex)
        {
            auto simulator = submodel.GetSimulator();
            const int32_t targetSimulatorId = simulator.FindOutgoingDependency("Final simulator").GetID();

            // Prepare for sync
            submodel.UpdateParentOnEntity(primedTote, Ers::Entity::InvalidEntity);

            SimulationTime delay(1);
            Ers::ApplyModelPrecision(delay);

            struct SendToFinalSubModelEventData : Ers::ISyncEvent<SendToFinalSubModelEventData>
            {
                EntityID PrimedTote;

                static const char* GetName() { return "Move to final submodel"; }

                void OnSenderSide() { PrimedTote = Ers::GetSubModel().SendEntity(Ers::SyncEvent::GetSyncEventTarget(), PrimedTote).id; }

                void OnTargetSide()
                {
                    // Inside the event body we have entered the target's submodel
                    auto& targetSubModel = Ers::GetSubModel();

                    // Take entities out of the channel
                    const Ers::Entity finalSubModelToat =
                        targetSubModel.ReceiveEntity(Ers::SyncEvent::GetSyncEventSender(), Ers::SentEntity(PrimedTote));

                    auto& context = targetSubModel.GetSubModelContext<SinkContext>();
                    Ers::Entity sinkEntity = context.SinkEntity;
                    auto* sinkProperties = sinkEntity.GetComponent<WealthOfRows::SinkPropertiesComponent>();

                    // Add toat to collection
                    auto& queue = sinkProperties->IncomingQueues.at(
                        Ers::SyncEvent::GetSyncEventSender()); // This only works because we aren't adding and removing submodels and the
                    // model is build in
                    // a
                    // specific  order. Otherwise a map is more suitable
                    const bool previouslyPresent = !queue.empty();
                    queue.emplace(finalSubModelToat);

                    if (previouslyPresent)
                    {
                        return;
                    }

                    for (const auto& receivedToatsCollection : sinkProperties->IncomingQueues)
                    {
                        if (receivedToatsCollection.empty())
                        {
                            return;
                        }
                    }

                    sinkProperties->ReceivedToats += sinkProperties->IncomingQueues.size();
                    for (auto& receivedToatsCollection : sinkProperties->IncomingQueues)
                    {

                        targetSubModel.DestroyEntity(receivedToatsCollection.front());
                        receivedToatsCollection.pop();
                    }
                    return;
                }
            };

            // Schedule sync event, please note that the SharedState is cached when multiple events that share this are scheduled.
            // The Shared State is intended to resolve entities, generate data or other heavy operations that don't have to be repeated.
            auto& data = Ers::EventScheduler::ScheduleSyncEvent<SendToFinalSubModelEventData>(delay, targetSimulatorId);
            data.PrimedTote = primedTote;

            if (properties->ConveyorIndex == 0)
            {
                return;
            }

            properties->AllowedToMoveOut = false;

            // Schedule event on the previous conveyor to keep shrink queue
            const EntityID& previousConveyor = statistics->Conveyors.at(properties->ConveyorIndex - 1);
            auto previousConveyorProperties = submodel.GetComponent<ConveyorPropertiesComponent>(previousConveyor);
            if (!previousConveyorProperties->AllowedToMoveOut)
            {
                return;
            }

            // When enough totes exist in previous conveyor notify that conveyor
            // This will trigger the move event early for the other conveyor to send it's tote to this conveyor immediatly
            if (previousConveyorProperties->ToteQueue.empty())
            {
                return;
            }

            const EntityID& previousConveyorToat = previousConveyorProperties->ToteQueue.front();
            submodel.GetComponent<ConveyorScriptBehavior>(previousConveyor)->MoveRequest(previousConveyorToat);
            return;
        }

        const EntityID& nextConveyor = statistics->Conveyors.at(properties->ConveyorIndex + 1);
        auto nextConveyorProperties = submodel.GetComponent<ConveyorPropertiesComponent>(nextConveyor);
        int childCount = submodel.HasComponent<Ers::RelationComponent>(nextConveyor)
            ? submodel.GetComponent<Ers::RelationComponent>(nextConveyor)->ChildCount()
            : 0;
        if (childCount >= nextConveyorProperties->Capacity)
        {
            return;
        }

        submodel.UpdateParentOnEntity(primedTote, nextConveyor);
        statistics->NumberOfMovedEntities++;

        if (properties->ConveyorIndex == 0)
        {
            return;
        }

        properties->AllowedToMoveOut = false;

        // Schedule event on the previous conveyor to keep shrink queue
        const EntityID& previousConveyor = statistics->Conveyors.at(properties->ConveyorIndex - 1);
        auto previousConveyorProperties = submodel.GetComponent<ConveyorPropertiesComponent>(previousConveyor);
        if (!previousConveyorProperties->AllowedToMoveOut)
        {
            return;
        }

        if (previousConveyorProperties->ToteQueue.empty())
        {
            return;
        }

        const EntityID& previousConveyorToat = previousConveyorProperties->ToteQueue.front();
        submodel.GetComponent<ConveyorScriptBehavior>(previousConveyor)->MoveRequest(previousConveyorToat);
    }

    const char* WealthOfRows::SubModelStatistics::StatisticsEntityName = "Statistics";

    void SubModelStatistics::OnStart()
    {
        auto& submodel = Ers::GetSubModel();

        OnEnteredConnection = submodel.Events().Relation().OnEntered().Connect<SubModelStatistics, &SubModelStatistics::OnEntered>(this);
        OnExitedConnection = submodel.Events().Relation().OnExited().Connect<SubModelStatistics, &SubModelStatistics::OnExited>(this);

        const EntityID statisticsEntity = submodel.FindEntity(SubModelStatistics::StatisticsEntityName);
        const EntityID firstConveyor = submodel.GetComponent<SubModelStatistics>(statisticsEntity)->Conveyors.at(0);

        auto properties = submodel.GetComponent<ConveyorPropertiesComponent>(firstConveyor);
        properties->AllowedToMoveOut = true;
        properties->ChanceOfDelay = 0;
        properties->MinimumTime = 0;
        properties->Capacity = 0;

        submodel.GetComponent<ConveyorScriptBehavior>(firstConveyor)->CreateToteEvent();
    }

    void SubModelStatistics::OnDestroy()
    {
        OnEnteredConnection.Disconnect();
        OnExitedConnection.Disconnect();
    }

    void SubModelStatistics::OnEntered(Ers::Entity parent, Ers::Entity newChild)
    {
        auto& submodel = Ers::GetSubModel();
        if (submodel.HasComponent<ConveyorScriptBehavior>(parent))
            submodel.GetComponent<ConveyorScriptBehavior>(parent)->OnEntered(newChild);
    }

    void SubModelStatistics::OnExited(Ers::Entity parent, Ers::Entity oldChild)
    {
        auto& submodel = Ers::GetSubModel();
        if (submodel.HasComponent<ConveyorScriptBehavior>(parent))
            submodel.GetComponent<ConveyorScriptBehavior>(parent)->OnExited(oldChild);
    }

    void CreateSubModel(Ers::Model::ModelContainer& modelContainer, int conveyorCount, uint64_t chanceOfDelay)
    {
        auto newSimulator =
            modelContainer.AddSimulator(std::to_string(modelContainer.GetSimulators().size()), Ers::SimulatorType::DiscreteEvent);

        auto& submodel = newSimulator.GetSubModel();
        submodel.EnterSubModel();

        submodel.AddComponentType<SubModelStatistics>();
        submodel.AddComponentType<ConveyorPropertiesComponent>();
        submodel.AddComponentType<ConveyorScriptBehavior>();
        submodel.AddComponentType<Ers::RelationComponent>();

        auto& sm = Ers::GetSubModel();
        const EntityID statisticsEntity = submodel.CreateEntity(SubModelStatistics::StatisticsEntityName);
        auto statisticProperties = submodel.AddComponent<SubModelStatistics>(statisticsEntity);

        for (size_t i = 0; i < conveyorCount + 1; i++)
        {
            const EntityID conveyorEntity = submodel.CreateEntity(std::format("Conveyor {}", i));

            auto properties = submodel.AddComponent<ConveyorPropertiesComponent>(conveyorEntity);
            properties->ConveyorIndex = statisticProperties->Conveyors.size();
            properties->ChanceOfDelay = chanceOfDelay;
            properties->StatisticsEntity = statisticsEntity;
            submodel.AddComponent<ConveyorScriptBehavior>(conveyorEntity);
            statisticProperties->Conveyors.emplace_back(conveyorEntity);
        }

        submodel.ExitSubModel();
    }

    void CreateFinalSubModel(Ers::Model::ModelContainer& modelContainer)
    {
        auto simulator = modelContainer.AddSimulator("Final simulator", Ers::SimulatorType::DiscreteEvent);

        auto& simulatorSubModel = simulator.GetSubModel();
        simulatorSubModel.EnterSubModel();

        simulatorSubModel.AddComponentType<SinkPropertiesComponent>();

        EntityID sinkEntity = simulatorSubModel.CreateEntity("Sink");
        auto sinkProperties = simulatorSubModel.AddComponent<SinkPropertiesComponent>(sinkEntity);

        auto& sinkContext = simulatorSubModel.AddSubModelContext<SinkContext>();
        sinkContext.SinkEntity = sinkEntity;

        sinkProperties->ReceivedToats = 0;

        // Add dependencies based on all other submodels that need to feed this submodel
        const size_t simulatorCount = modelContainer.GetSimulators().size() - 1;
        for (size_t i = 0; i < simulatorCount; i++)
        {
            const std::string simulatorName = std::to_string(i);
            auto dependencySimulator = modelContainer.FindSimulator(simulatorName);
            if (dependencySimulator.Valid())
            {
                modelContainer.AddSimulatorDependency(dependencySimulator, simulator);
                SimulationTime minimalDelay(1);
                Ers::ApplyModelPrecision(minimalDelay);
                auto& dependencySubModel = dependencySimulator.GetSubModel();
                dependencySubModel.EnterSubModel();
                Ers::EventScheduler::SetPromise(simulator.GetID(), minimalDelay);
                dependencySubModel.ExitSubModel();
            }
            sinkProperties->IncomingQueues.emplace_back(); // Add a new queue for each incoming conveyor line
        }

        simulatorSubModel.ExitSubModel();
    }
} // namespace WealthOfRows

void MeasureUser(int submodelCount, int conveyorCount, SimulationTime endTimeForModel, uint64_t chanceOfDelay)
{
    Ers::Model::ModelManager& manager = Ers::Model::GetModelManager();
    Ers::Model::ModelContainer modelContainer = Ers::Model::ModelContainer::CreateModelContainer();
    modelContainer.SetPrecision(1'000'000);

    modelContainer.SetSeed(1);

    Ers::Logger::Info(std::format("{}S_{}C_{}T_{}D", submodelCount, conveyorCount, endTimeForModel, chanceOfDelay));
    Ers::Logger::Debug("Creating model...");

    for (int i = 0; i < submodelCount; i++)
    {
        WealthOfRows::CreateSubModel(modelContainer, conveyorCount, chanceOfDelay);
    }
    WealthOfRows::CreateFinalSubModel(modelContainer);

#ifdef WOR_DEBUGGER
    auto debugger = Ers::Debugging::Debugger(modelContainer);
    debugger.SetStepSize(1'000'000);
    while (!debugger.WantsClose())
    {
        debugger.Update();
    }
    return;
#endif

    Ers::Logger::Debug("Starting...");

    manager.AddModelContainer(modelContainer, endTimeForModel * modelContainer.GetPrecision());

    Ers::Logger::Debug("Started!");
    const std::chrono::high_resolution_clock::time_point startTime = std::chrono::high_resolution_clock::now();

    manager.RunWithProgressBar();

    const std::chrono::high_resolution_clock::time_point endTimePoint = std::chrono::high_resolution_clock::now();
    auto& finalSubmodel = modelContainer.GetSimulators().at(modelContainer.GetSimulators().size() - 1).GetSubModel();
    finalSubmodel.EnterSubModel();
    const EntityID sinkEntity = finalSubmodel.FindEntity("Sink");
    auto sinkProperties = finalSubmodel.GetComponent<WealthOfRows::SinkPropertiesComponent>(sinkEntity);

    Ers::Logger::Info(
        std::format("{} received toats", sinkProperties->ReceivedToats) + " " +
        std::format(
            "{} s",
            std::to_string(
                static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>((endTimePoint - startTime)).count()) / 1000)));
    finalSubmodel.ExitSubModel();

    for (int i = 0; i < submodelCount; i++)
    {
        auto simulator = modelContainer.GetSimulators()[i];
        auto& conveyorSubmodel = simulator.GetSubModel();
        conveyorSubmodel.EnterSubModel();
        auto statisticsEntity = conveyorSubmodel.FindEntity(WealthOfRows::SubModelStatistics::StatisticsEntityName);
        auto statistics = conveyorSubmodel.GetComponent<WealthOfRows::SubModelStatistics>(statisticsEntity);
        Ers::Logger::Info(
            std::format(
                "[{}] Totes generated: {}, Moved: {}", simulator.GetName(), statistics->NumberOfGeneratedEntities,
                statistics->NumberOfGeneratedEntities - (statistics->NumberOfMovedEntities / conveyorCount)));
        conveyorSubmodel.DestroyEntity(statisticsEntity);
        conveyorSubmodel.ExitSubModel();
    }

    std::cout << "\n";

    Ers::Logger::Debug("Destroying model...");
}

void MeasureUser(
    const int& submodelCount, const int& conveyorCount, const SimulationTime endTimeForModel, uint64_t chanceOfDelay, uint64_t amountOfRuns)
{
    for (size_t i = 0; i < amountOfRuns; i++)
    {
        MeasureUser(submodelCount, conveyorCount, endTimeForModel, chanceOfDelay);
    }
}

int main()
{
    Ers::InitializeAPI();
    // Benchmark settings
    const int submodelCount = 50;
    const int conveyorCount = 10;
    const int chanceOfDelay = 3;
    SimulationTime endTimeForModel(86400);

    for (int i = 0; i < 1; i++)
        MeasureUser(submodelCount, conveyorCount, endTimeForModel, chanceOfDelay);

    return 0;
}
