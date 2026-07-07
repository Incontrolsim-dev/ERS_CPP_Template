#include "Ers/Logger.h"
#include "Ers/Model/ModelContainer.h"
#include "Ers/Model/ModelManager.h"
#include "Ers/Model/Simulator/Simulator.h"
#include "Ers/SubModel/Component/GlobalComponentTypes.h"
#include "Ers/SubModel/DataComponent.h"
#include "Ers/SubModel/EventScheduler.h"
#include "Ers/SubModel/ScriptBehaviorComponent.h"
#include "Ers/SubModel/SubModel.h"

#include <format>
#include <queue>

namespace MoverModelSync
{
    struct BinComponent : public Ers::DataComponent
    {
        uint64_t Stored;

        bool operator==(const BinComponent& other) const { return this == &other; }
    };
    
    struct MoveLocalEvent
    {
        EntityID Source;
        EntityID Target;
        uint32_t nMoving;

        void OnEvent();

        ERS_EVENT(Source, Target, nMoving)
    };


    // Data send via the sync event
    struct MoverModelSyncEvent : Ers::ISyncEvent<MoverModelSyncEvent>
    {
        uint64_t NumberMoving;

        static const char* GetName() { return "Move to target"; }

        void OnSenderSide()
        {
            // This event is executed in the source submodel. This function is intended to gather the state from the source to send it
            // to the target. This event is called on the exact time as the target executes the sync event The event appears to be
            // instantaneous for both the source and target
        }

        void OnTargetSide()
        {
            // Get the target submodel, which will be receiving data
            auto& targetSubModel = Ers::SubModel::Get();

            // TODO(sync): Use SubModelContext. This is slow.
            Ers::Entity targetBinEntity = targetSubModel.FindEntity("Target bin");

            // Store object in target bin
            auto* targetBin = targetBinEntity.GetComponent<BinComponent>();
            targetBin->Stored += NumberMoving;
        }

        ERS_EVENT(NumberMoving)
    };

    class MoveBehaviour : public Ers::ScriptBehaviorComponent
    {
    public:
        MoveBehaviour() = default;

        void OnStart();

        EntityID Source{};
        EntityID Target{};

        uint32_t nMoving = 1;
    };

    void MoveBehaviour::OnStart()
    {
        MoveLocalEvent eventData;
        eventData.Source = Source;
        eventData.Target = Target;
        eventData.nMoving = nMoving;
        Ers::EventScheduler::ScheduleLocalEvent(0, 0, eventData);
    }

    void MoveLocalEvent::OnEvent()
    {
        auto& sourceSubModel = Ers::SubModel::Get();
        auto sourceBin = sourceSubModel.GetComponent<BinComponent>(Source);
        if (sourceBin->Stored == 0)
            return; // Can't move objects if there are none

        sourceBin->Stored -= nMoving;

        auto simulator = sourceSubModel.GetSimulator();
        const int32_t targetSimulatorId = simulator.FindOutgoingDependency("Target Simulator").GetID();

        // Send object to target bin in other simulator, via sync event
        SimulationTime noDelay(1);
        MoverModelSyncEvent data;
        data.NumberMoving = nMoving;
        Ers::EventScheduler::ScheduleSyncEvent<MoverModelSyncEvent>(noDelay, targetSimulatorId, data);

        // Repeat MoveEvent
        double random                  = sourceSubModel.SampleRandomGenerator() * sourceSubModel.GetModelPrecision();
        const SimulationTime delayTime = SimulationTime(random);
        Ers::EventScheduler::ScheduleLocalEvent(0, delayTime, MoveLocalEvent{Source, Target, nMoving});
    }
} // namespace MoverModelSync

int main()
{
    Ers::Initialize();

    // Register types
    Ers::ComponentRegistry<MoverModelSync::BinComponent>::Register();
    Ers::ComponentRegistry<MoverModelSync::MoveBehaviour>::Register();
    Ers::EventScheduler::RegisterLocalEvent<MoverModelSync::MoveLocalEvent>();
    Ers::EventScheduler::RegisterSyncEvent<MoverModelSync::MoverModelSyncEvent>();

    const uint64_t nObjects = 10000;
    auto endTimeForModel = SimulationTime(10000);
    endTimeForModel *= 1'000'000; // Apply model precision

    Ers::ModelManager& manager = Ers::ModelManager::Get();
    Ers::ModelContainer modelContainer = Ers::ModelContainer::Create();

    // Create simulators and get the submodels
    auto sourceSimulator = modelContainer.AddSimulator("Source Simulator", Ers::SimulatorType::DiscreteEvent);
    auto targetSimulator = modelContainer.AddSimulator("Target Simulator", Ers::SimulatorType::DiscreteEvent);

    // Create source bin and fill it with objects
    sourceSimulator.EnterSubModel();
    const EntityID sourceEntity = Ers::SubModel::Get().CreateEntity("Source bin");
    auto source = Ers::SubModel::Get().AddComponent<MoverModelSync::BinComponent>(sourceEntity);
    source->Stored = nObjects;

    // Create target bin and leave it empty
    targetSimulator.EnterSubModel();
    const EntityID targetEntity = Ers::SubModel::Get().CreateEntity("Target bin");
    auto target = Ers::SubModel::Get().AddComponent<MoverModelSync::BinComponent>(targetEntity);
    targetSimulator.ExitSubModel();

    // Create mover and set source and target to move from and to
    const EntityID moverEntity = Ers::SubModel::Get().CreateEntity("Mover");
    auto mover = Ers::SubModel::Get().AddComponent<MoverModelSync::MoveBehaviour>(moverEntity);
    mover->Source = sourceEntity;
    mover->Target = targetEntity;

    sourceSimulator.ExitSubModel();

    // Add source simulator as dependency to target simulator, required for sync event
    modelContainer.AddSimulatorDependency(sourceSimulator, targetSimulator);

    Ers::Logger::Info(std::format("Source bin has {} objects, Target bin has {} objects", source->Stored, target->Stored));

    Ers::Logger::Debug("Starting...");
    manager.AddModelContainer(modelContainer, endTimeForModel);

    while (manager.Count() > 0)
    {
        manager.Update();
    }

    sourceSimulator.EnterSubModel();
    source = Ers::SubModel::Get().GetComponent<MoverModelSync::BinComponent>(sourceEntity);
    sourceSimulator.ExitSubModel();
    targetSimulator.EnterSubModel();
    target = Ers::SubModel::Get().GetComponent<MoverModelSync::BinComponent>(targetEntity);
    targetSimulator.ExitSubModel();
    Ers::Logger::Info(std::format("Source bin has {} objects, Target bin has {} objects", source->Stored, target->Stored));

    Ers::Uninitialize();
    return 0;
}
