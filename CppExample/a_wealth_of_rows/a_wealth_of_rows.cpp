#include <chrono>
#include <cmath>
#include <cstdint>
#include <format>
#include <iostream>
#include <queue>

#include "Ers/Api.h"
#include "Ers/Debugging/Debugger.h"
#include "Ers/Debugging/Profiler.h"
#include "Ers/Model/ModelContainer.h"
#include "Ers/Model/ModelManager.h"
#include "Ers/SubModel/Component/RelationComponent.h"
#include "Ers/SubModel/DataComponent.h"
#include "Ers/SubModel/Entity.h"
#include "Ers/SubModel/ErsEvent.h"
#include "Ers/SubModel/EventScheduler.h"
#include "Ers/SubModel/ScriptBehaviorComponent.h"
#include "Ers/SubModel/TypeInfo.h"
#include "Ers/UI/Window.h"
#include "Ers/Utility/Util.h"

#include "Ers/Model/ModelManager.h"

#include "Ers/Model/Simulator/Simulator.h"
#include "Ers/SubModel/SubModel.h"

#include "Ers/External/ImGuiCpp.hpp"
#include "Ers/External/ImPlotCpp.hpp"
#include "Ers/Logger.h"

#ifdef WOR_DEBUGGER
#include "Ers/Systems/RenderSystem.h"
#endif

namespace WealthOfRows
{
    struct DebugUiState
    {
        int SubmodelCount{50};
        int ConveyorCount{10};
        int ChanceOfDelay{3};
        double EndTimeSeconds{86400.0};
    };

    inline DebugUiState g_DebugUiState{};

    // Forward declaration
    class ConveyorScriptBehavior;
    class SubModelStatistics : public Ers::ScriptBehaviorComponent
    {
      public:
        SubModelStatistics() :
            NumberOfGeneratedEntities(0),
            NumberOfMovedEntities(0),
            HasStartedInitialization(false)
        {
        }

        void OnStart();
        void Serialization(Ers::Serializer node) override;

        uint64_t NumberOfGeneratedEntities;
        uint64_t NumberOfMovedEntities;
        std::vector<EntityID> Conveyors;
        bool HasStartedInitialization;

        static const char* StatisticsEntityName;
    };

    struct SinkPropertiesComponent : public Ers::ScriptBehaviorComponent
    {

        uint64_t ReceivedTotes{0};
        std::vector<std::queue<EntityID>> IncomingQueues;

        bool operator==(const SinkPropertiesComponent& other) const { return this == &other; }

        void Serialization(Ers::Serializer node) override;
    };

    struct ConveyorPropertiesComponent : public Ers::DataComponent
    {

        uint64_t Capacity{1};
        uint64_t MinimumTime{2};
        uint64_t ChanceOfDelay{0};
        uint64_t DelayTimeMin{1};
        uint64_t DelayTimeMax{10};
        bool AllowedToMoveOut{false};

        uint64_t ConveyorIndex{0};
        EntityID StatisticsEntity{Ers::Entity::InvalidEntity};

        bool operator==(const ConveyorPropertiesComponent& other) const { return this == &other; }

        static Ers::TypeInfo* GetTypeInfo()
        {
            Ers::TypeInfo* conveyorPropertiesTypeInfo = Ers::TypeInfo::RegisterStruct("conveyor_properties");
            conveyorPropertiesTypeInfo->AddField("capacity", Ers::FieldType::Int64, offsetof(ConveyorPropertiesComponent, Capacity));
            conveyorPropertiesTypeInfo->AddField("minimum_time", Ers::FieldType::Int64, offsetof(ConveyorPropertiesComponent, MinimumTime));
            conveyorPropertiesTypeInfo->AddField(
                "chance_of_delay", Ers::FieldType::Int64, offsetof(ConveyorPropertiesComponent, ChanceOfDelay));
            conveyorPropertiesTypeInfo->AddField(
                "delay_time_min", Ers::FieldType::Int64, offsetof(ConveyorPropertiesComponent, DelayTimeMin));
            conveyorPropertiesTypeInfo->AddField(
                "delay_time_max", Ers::FieldType::Int64, offsetof(ConveyorPropertiesComponent, DelayTimeMax));
            conveyorPropertiesTypeInfo->AddField(
                "allowed_to_move_out", Ers::FieldType::Bool, offsetof(ConveyorPropertiesComponent, AllowedToMoveOut));
            conveyorPropertiesTypeInfo->AddField(
                "conveyor_index", Ers::FieldType::Int64, offsetof(ConveyorPropertiesComponent, ConveyorIndex), true);
            // Serialize StatisticsEntity so the cached reference is preserved across save/load
            conveyorPropertiesTypeInfo->AddField(
                "statistics_entity", Ers::FieldType::Int64, offsetof(ConveyorPropertiesComponent, StatisticsEntity));

            return conveyorPropertiesTypeInfo;
        }
    };

    class ConveyorScriptBehavior : public Ers::ScriptBehaviorComponent
    {

      public:
        ConveyorScriptBehavior();

        void OnAwake() override;
        void OnDestroy() override;

        void OnStart() override;
        void CreateToteEvent();

        void OnEntered(EntityID newChild) override;
        void OnExited(EntityID oldChild) override;

        void Serialization(Ers::Serializer node) override;

        // Contains all entities currently present in this conveyor
        std::queue<EntityID> ToteQueue;

        void DelayOrMove(const EntityID& primedTote);
        void MoveRequest(const EntityID& primedTote);
    };

    // Event to trigger CreateToteEvent on ConveyorScriptBehavior
    struct TriggerCreateToteEvent
    {
        EntityID entity;

        void OnEvent()
        {
            auto& submodel = Ers::SubModel::Get();
            auto* self     = submodel.GetComponent<ConveyorScriptBehavior>(entity);
            self->CreateToteEvent();
        }

        ERS_EVENT(entity)
    };

    // Event to trigger DelayOrMove on ConveyorScriptBehavior
    struct TriggerDelayOrMoveEvent
    {
        EntityID entity;
        EntityID child;

        void OnEvent()
        {
            auto& submodel = Ers::SubModel::Get();
            auto* self     = submodel.GetComponent<ConveyorScriptBehavior>(entity);
            self->DelayOrMove(child);
        }

        ERS_EVENT(entity, child)
    };

    struct SinkContext
    {
        EntityID SinkEntity;

        // Constructor for automatic initialization after loading
        SinkContext() { SinkEntity = Ers::SubModel::Get().FindEntity("Sink"); }
    };

    struct SendToFinalSubModelEventData : Ers::ISyncEvent<SendToFinalSubModelEventData>
    {
        EntityID PrimedTote;

        static const char* GetName() { return "Move to final submodel"; }

        void OnSenderSide() { PrimedTote = Ers::SubModel::Get().SendEntity(Ers::SyncEvent::GetSyncEventTarget(), PrimedTote).id; }

        void OnTargetSide()
        {
            // Inside the event body we have entered the target's submodel
            auto& targetSubModel = Ers::SubModel::Get();

            // Take entities out of the channel
            const Ers::Entity finalSubModelTote =
                targetSubModel.ReceiveEntity(Ers::SyncEvent::GetSyncEventSender(), Ers::SentEntity(PrimedTote));

            auto& context          = targetSubModel.GetSubModelContext<SinkContext>();
            Ers::Entity sinkEntity = context.SinkEntity;
            auto* sinkProperties   = sinkEntity.GetComponent<WealthOfRows::SinkPropertiesComponent>();

            // Add tote to collection
            auto& queue = sinkProperties->IncomingQueues.at(
                Ers::SyncEvent::GetSyncEventSender()); // This only works because we aren't adding and removing submodels and the
                                                       // model is build in
                                                       // a
                                                       // specific  order. Otherwise a map is more suitable
            const bool previouslyPresent = !queue.empty();
            queue.emplace(finalSubModelTote);

            if (previouslyPresent)
            {
                return;
            }

            for (const auto& receivedTotesCollection : sinkProperties->IncomingQueues)
            {
                if (receivedTotesCollection.empty())
                {
                    return;
                }
            }

            sinkProperties->ReceivedTotes += sinkProperties->IncomingQueues.size();
            for (auto& receivedTotesCollection : sinkProperties->IncomingQueues)
            {

                targetSubModel.DestroyEntity(receivedTotesCollection.front());
                receivedTotesCollection.pop();
            }
            return;
        }

        ERS_EVENT(PrimedTote)
    };

    ConveyorScriptBehavior::ConveyorScriptBehavior()
    {
    }

    void ConveyorScriptBehavior::OnAwake()
    {
        auto& submodel = Ers::SubModel::Get();

        auto properties = submodel.GetComponent<ConveyorPropertiesComponent>(ConnectedEntity);

        // Resolve and cache the statistics entity reference
        // This works for both model creation and loading, since StatisticsEntity is not serialized
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
        auto& submodel  = Ers::SubModel::Get();
        auto properties = submodel.GetComponent<ConveyorPropertiesComponent>(ConnectedEntity);

        const EntityID tote = submodel.CreateEntity("");

        auto& sm        = Ers::SubModel::Get();
        auto statistics = submodel.GetComponent<SubModelStatistics>(properties->StatisticsEntity);
        statistics->NumberOfGeneratedEntities++;

        submodel.UpdateParentOnEntity(tote, ConnectedEntity);

        SimulationTime eventDelay =
            std::round(submodel.SampleRandomGenerator() * static_cast<double>(1'000'000)) * submodel.GetModelPrecision();
        eventDelay /= SimulationTime(100000);

        Ers::EventScheduler::ScheduleLocalEvent(0, eventDelay, TriggerCreateToteEvent{ConnectedEntity});
    }

    void ConveyorScriptBehavior::OnEntered(EntityID newChild)
    {
        auto& submodel  = Ers::SubModel::Get();
        auto properties = submodel.GetComponent<ConveyorPropertiesComponent>(ConnectedEntity);

        ToteQueue.emplace(newChild);

        if (properties->ConveyorIndex != 0)
        {
            // add delay
            SimulationTime timespan = properties->MinimumTime * submodel.GetModelPrecision();

            // Schedule events to advance the totes in the queue
            Ers::EventScheduler::ScheduleLocalEvent(0, timespan, TriggerDelayOrMoveEvent{ConnectedEntity, newChild});
        }
        else
        {
            MoveRequest(newChild);
        }
    }

    void ConveyorScriptBehavior::OnExited(EntityID oldChild)
    {
        auto& submodel = Ers::SubModel::Get();

        auto properties = submodel.GetComponent<ConveyorPropertiesComponent>(ConnectedEntity);

        ToteQueue.pop();

        // This is an implicit check for sources
        if (properties->Capacity > 1)
        {
            properties->AllowedToMoveOut = true;
        }
    }

    void ConveyorScriptBehavior::Serialization(Ers::Serializer node)
    {
        // Save/load tote queue using helper
        node.Serialize("tote_queue", ToteQueue);
    }

    void ConveyorScriptBehavior::DelayOrMove(const EntityID& primedTote)
    {
        auto& submodel = Ers::SubModel::Get();

        auto properties = submodel.GetComponent<ConveyorPropertiesComponent>(ConnectedEntity);

        // Add randomized delay
        if (submodel.SampleRandomGenerator() * 100.0 <= static_cast<double>(properties->ChanceOfDelay))
        {
            SimulationTime randomDelay((submodel.SampleRandomGenerator() * 100000) / 100000);

            randomDelay *= SimulationTime(properties->DelayTimeMax - properties->DelayTimeMin);

            SimulationTime delay(properties->DelayTimeMin);
            delay += randomDelay;
            delay *= submodel.GetModelPrecision();

            Ers::EventScheduler::ScheduleLocalEvent(0, delay, TriggerDelayOrMoveEvent{ConnectedEntity, primedTote});
            return;
        }

        properties->AllowedToMoveOut = true;

        MoveRequest(primedTote);
    }

    void ConveyorScriptBehavior::MoveRequest(const EntityID& primedTote)
    {
        auto& submodel = Ers::SubModel::Get();

        auto properties = submodel.GetComponent<ConveyorPropertiesComponent>(ConnectedEntity);

        if (!properties->AllowedToMoveOut)
        {
            return;
        }

        auto simulator       = submodel.GetSimulator();
        uint32_t simulatorId = simulator.GetID();

        auto statistics = submodel.GetComponent<SubModelStatistics>(properties->StatisticsEntity);

        if (statistics->Conveyors.size() - 1 == properties->ConveyorIndex)
        {
            auto simulator                  = submodel.GetSimulator();
            const int32_t targetSimulatorId = simulator.FindOutgoingDependency("Final simulator").GetID();

            // Prepare for sync
            submodel.UpdateParentOnEntity(primedTote, Ers::Entity::InvalidEntity);

            SimulationTime delay = 1 * submodel.GetModelPrecision();

            // Schedule sync event, please note that the SharedState is cached when multiple events that share this are scheduled.
            // The Shared State is intended to resolve entities, generate data or other heavy operations that don't have to be repeated.
            SendToFinalSubModelEventData syncData;
            syncData.PrimedTote = primedTote;
            Ers::EventScheduler::ScheduleSyncEvent<SendToFinalSubModelEventData>(delay, targetSimulatorId, syncData);

            if (properties->ConveyorIndex == 0)
            {
                return;
            }

            properties->AllowedToMoveOut = false;

            // Schedule event on the previous conveyor to keep shrink queue
            const EntityID& previousConveyor     = statistics->Conveyors.at(properties->ConveyorIndex - 1);
            auto previousConveyorProperties      = submodel.GetComponent<ConveyorPropertiesComponent>(previousConveyor);
            auto previousConveyorScriptBehaviour = submodel.GetComponent<ConveyorScriptBehavior>(previousConveyor);
            if (!previousConveyorProperties->AllowedToMoveOut)
            {
                return;
            }

            // When enough totes exist in previous conveyor notify that conveyor
            // This will trigger the move event early for the other conveyor to send it's tote to this conveyor immediately
            if (previousConveyorScriptBehaviour->ToteQueue.empty())
            {
                return;
            }

            const EntityID& previousConveyorTote = previousConveyorScriptBehaviour->ToteQueue.front();
            submodel.GetComponent<ConveyorScriptBehavior>(previousConveyor)->MoveRequest(previousConveyorTote);
            return;
        }

        const EntityID& nextConveyor = statistics->Conveyors.at(properties->ConveyorIndex + 1);
        auto nextConveyorProperties  = submodel.GetComponent<ConveyorPropertiesComponent>(nextConveyor);
        int childCount               = submodel.HasComponent<Ers::RelationComponent>(nextConveyor)
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
        const EntityID& previousConveyor     = statistics->Conveyors.at(properties->ConveyorIndex - 1);
        auto previousConveyorProperties      = submodel.GetComponent<ConveyorPropertiesComponent>(previousConveyor);
        auto previousConveyorScriptBehaviour = submodel.GetComponent<ConveyorScriptBehavior>(previousConveyor);
        if (!previousConveyorProperties->AllowedToMoveOut)
        {
            return;
        }

        if (previousConveyorScriptBehaviour->ToteQueue.empty())
        {
            return;
        }

        const EntityID& previousConveyorTote = previousConveyorScriptBehaviour->ToteQueue.front();
        submodel.GetComponent<ConveyorScriptBehavior>(previousConveyor)->MoveRequest(previousConveyorTote);
    }

    const char* WealthOfRows::SubModelStatistics::StatisticsEntityName = "Statistics";

    void SubModelStatistics::OnStart()
    {
        auto& submodel = Ers::SubModel::Get();

        const EntityID statisticsEntity = submodel.FindEntity(SubModelStatistics::StatisticsEntityName);
        const EntityID firstConveyor    = submodel.GetComponent<SubModelStatistics>(statisticsEntity)->Conveyors.at(0);

        auto properties              = submodel.GetComponent<ConveyorPropertiesComponent>(firstConveyor);
        properties->AllowedToMoveOut = true;
        properties->ChanceOfDelay    = 0;
        properties->MinimumTime      = 0;
        properties->Capacity         = 0;

        // Only create the initial tote if we haven't already done so
        // This prevents duplicate totes when loading a saved model
        if (!HasStartedInitialization)
        {
            submodel.GetComponent<ConveyorScriptBehavior>(firstConveyor)->CreateToteEvent();
            HasStartedInitialization = true;
        }
    }

    void SubModelStatistics::Serialization(Ers::Serializer node)
    {
        // Save/load statistics counters
        node.Serialize("num_generated", NumberOfGeneratedEntities);
        node.Serialize("num_moved", NumberOfMovedEntities);

        // Save/load conveyor entity IDs using helper
        node.Serialize("conveyors", Conveyors);

        // Save/load initialization flag to prevent duplicate tote creation
        node.Serialize("has_started_initialization", HasStartedInitialization);
    }

    void SinkPropertiesComponent::Serialization(Ers::Serializer node)
    {
        // Save/load received totes counter
        node.Serialize("received_totes", ReceivedTotes);

        // Save/load incoming queues - recursive serialization handles nested vector<queue<EntityID>>
        node.Serialize("incoming_queues", IncomingQueues);
    }

    void CreateSubModel(Ers::ModelContainer& modelContainer, int conveyorCount, uint64_t chanceOfDelay)
    {
        auto newSimulator =
            modelContainer.AddSimulator(std::to_string(modelContainer.GetSimulators().size()), Ers::SimulatorType::DiscreteEvent);

        newSimulator.EnterSubModel();
        auto& submodel = Ers::SubModel::Get();

        auto& sm                        = Ers::SubModel::Get();
        const EntityID statisticsEntity = submodel.CreateEntity(SubModelStatistics::StatisticsEntityName);
        auto statisticProperties        = submodel.AddComponent<SubModelStatistics>(statisticsEntity);

        for (size_t i = 0; i < conveyorCount + 1; i++)
        {
            const EntityID conveyorEntity = submodel.CreateEntity(std::format("Conveyor {}", i));

            auto properties              = submodel.AddComponent<ConveyorPropertiesComponent>(conveyorEntity);
            properties->ConveyorIndex    = statisticProperties->Conveyors.size();
            properties->ChanceOfDelay    = chanceOfDelay;
            properties->StatisticsEntity = statisticsEntity;
            submodel.AddComponent<ConveyorScriptBehavior>(conveyorEntity);
            statisticProperties->Conveyors.emplace_back(conveyorEntity);
        }

        newSimulator.ExitSubModel();
    }

    void CreateFinalSubModel(Ers::ModelContainer& modelContainer)
    {
        auto simulator = modelContainer.AddSimulator("Final simulator", Ers::SimulatorType::DiscreteEvent);

        simulator.EnterSubModel();
        auto& submodel = Ers::SubModel::Get();

        EntityID sinkEntity = submodel.CreateEntity("Sink");
        auto sinkProperties = submodel.AddComponent<SinkPropertiesComponent>(sinkEntity);

        sinkProperties->ReceivedTotes = 0;

        // Add dependencies based on all other submodels that need to feed this submodel
        const size_t simulatorCount = modelContainer.GetSimulators().size() - 1;
        for (size_t i = 0; i < simulatorCount; i++)
        {
            const std::string simulatorName = std::to_string(i);
            auto dependencySimulator        = modelContainer.FindSimulator(simulatorName);
            if (dependencySimulator.Valid())
            {
                modelContainer.AddSimulatorDependency(dependencySimulator, simulator);
                SimulationTime minimalDelay = 1 * submodel.GetModelPrecision();
                dependencySimulator.EnterSubModel();
                Ers::EventScheduler::SetPromise(simulator.GetID(), minimalDelay);
                dependencySimulator.ExitSubModel();
            }
            sinkProperties->IncomingQueues.emplace_back(); // Add a new queue for each incoming conveyor line
        }

        simulator.ExitSubModel();
    }

} // namespace WealthOfRows

void MeasureUser(int submodelCount, int conveyorCount, SimulationTime endTimeForModel, uint64_t chanceOfDelay)
{
    Ers::ModelManager& manager         = Ers::ModelManager::Get();
    Ers::ModelContainer modelContainer = Ers::ModelContainer::Create();
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
    Ers::Debugger::Run(modelContainer);
    return;
#endif

    Ers::Logger::Debug("Starting...");

    manager.AddModelContainer(modelContainer, endTimeForModel * modelContainer.GetPrecision());

    Ers::Logger::Debug("Started!");
    const std::chrono::high_resolution_clock::time_point startTime = std::chrono::high_resolution_clock::now();

    manager.RunWithProgressBar();

    const std::chrono::high_resolution_clock::time_point endTimePoint = std::chrono::high_resolution_clock::now();
    auto finalSimulator = modelContainer.GetSimulators().at(modelContainer.GetSimulators().size() - 1);
    finalSimulator.EnterSubModel();
    auto& finalSubmodel = Ers::SubModel::Get();

    const EntityID sinkEntity = finalSubmodel.FindEntity("Sink");
    auto sinkProperties       = finalSubmodel.GetComponent<WealthOfRows::SinkPropertiesComponent>(sinkEntity);

    Ers::Logger::Info(
        std::format("{} received totes", sinkProperties->ReceivedTotes) + " " +
        std::format(
            "{} s",
            std::to_string(
                static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>((endTimePoint - startTime)).count()) / 1000)));
    finalSimulator.ExitSubModel();

    for (int i = 0; i < submodelCount; i++)
    {
        auto simulator = modelContainer.GetSimulators()[i];
        simulator.EnterSubModel();
        auto& conveyorSubmodel = Ers::SubModel::Get();
        auto statisticsEntity  = conveyorSubmodel.FindEntity(WealthOfRows::SubModelStatistics::StatisticsEntityName);
        auto statistics        = conveyorSubmodel.GetComponent<WealthOfRows::SubModelStatistics>(statisticsEntity);
        Ers::Logger::Info(std::format(
            "[{}] Totes generated: {}, Moved: {}", simulator.GetName(), statistics->NumberOfGeneratedEntities,
            statistics->NumberOfGeneratedEntities - (statistics->NumberOfMovedEntities / conveyorCount)));
        conveyorSubmodel.DestroyEntity(statisticsEntity);
        simulator.ExitSubModel();
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
    Ers::Initialize();

    // Register event types before simulation starts
    Ers::EventScheduler::RegisterLocalEvent<WealthOfRows::TriggerCreateToteEvent>();
    Ers::EventScheduler::RegisterLocalEvent<WealthOfRows::TriggerDelayOrMoveEvent>();
    Ers::EventScheduler::RegisterSyncEvent<WealthOfRows::SendToFinalSubModelEventData>();

    // Register component types
    Ers::ComponentRegistry<WealthOfRows::SubModelStatistics>::Register();
    Ers::ComponentRegistry<WealthOfRows::ConveyorPropertiesComponent>::Register();
    Ers::ComponentRegistry<WealthOfRows::ConveyorScriptBehavior>::Register();
    Ers::ComponentRegistry<WealthOfRows::SinkPropertiesComponent>::Register();

    // Benchmark settings
    const int submodelCount = 50;
    const int conveyorCount = 10;
    const int chanceOfDelay = 3;
    SimulationTime endTimeForModel(86400);

    for (int i = 0; i < 1; i++)
        MeasureUser(submodelCount, conveyorCount, endTimeForModel, chanceOfDelay);

    Ers::Uninitialize();
    return 0;
}
