#include "Ers/Logger.h"
#include "Ers/Model/ModelContainer.h"
#include "Ers/Model/ModelManager.h"
#include "Ers/Model/Simulator/Simulator.h"
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
            auto& targetSubModel = Ers::GetSubModel();

            // TODO(sync): Use SubModelContext. This is slow.
            Ers::Entity targetBinEntity = targetSubModel.FindEntity("Target bin");

            // Store object in target bin
            auto* targetBin = targetBinEntity.GetComponent<BinComponent>();
            targetBin->Stored += NumberMoving;
        }
    };

    class MoveBehaviour : public Ers::ScriptBehaviorComponent
    {
    public:
        MoveBehaviour() = default;

        void OnStart();
        void OnDestroy();

        void MoveEvent();

        EntityID Source{};
        EntityID Target{};

        uint32_t nMoving = 1;
    };

    void MoveBehaviour::OnStart()
    {
        MoveEvent();
    }

    void MoveBehaviour::OnDestroy()
    {
    }

    void MoveBehaviour::MoveEvent()
    {
        auto& sourceSubModel = Ers::GetSubModel();
        auto sourceBin = sourceSubModel.GetComponent<BinComponent>(Source);
        if (sourceBin->Stored == 0)
            return; // Can't move objects if there are none

        sourceBin->Stored -= nMoving;

        auto simulator = sourceSubModel.GetSimulator();
        const int32_t targetSimulatorId = simulator.FindOutgoingDependency("Target Simulator").GetID();

        // Send object to target bin in other simulator, via sync event
        SimulationTime noDelay(1);
        auto& data = Ers::EventScheduler::ScheduleSyncEvent<MoverModelSyncEvent>(noDelay, targetSimulatorId);
        data.NumberMoving = nMoving;

        // Repeat MoveEvent
        const double random = sourceSubModel.SampleRandomGenerator();
        SimulationTime delayTime(random * static_cast<double>(1'000'000));
        Ers::EventScheduler::ScheduleLocalEvent(0, delayTime, [this]() { MoveEvent(); });
    }
} // namespace MoverModelSync

int main()
{
    Ers::Initialize();

    const uint64_t nObjects = 10000;
    auto endTimeForModel = SimulationTime(10000);
    endTimeForModel *= 1'000'000; // Apply model precision

    Ers::Model::ModelManager& manager = Ers::Model::GetModelManager();
    Ers::Model::ModelContainer modelContainer = Ers::Model::ModelContainer::CreateModelContainer();

    // Create simulators and get the submodels
    auto sourceSimulator = modelContainer.AddSimulator("Source Simulator", Ers::SimulatorType::DiscreteEvent);
    auto targetSimulator = modelContainer.AddSimulator("Target Simulator", Ers::SimulatorType::DiscreteEvent);

    auto& submodel = Ers::GetSubModel();

    // Register types
    sourceSimulator.EnterSubModel();
    Ers::GetSubModel().AddComponentType<MoverModelSync::BinComponent>();
    Ers::GetSubModel().AddComponentType<MoverModelSync::MoveBehaviour>();
    targetSimulator.EnterSubModel();
    Ers::GetSubModel().AddComponentType<MoverModelSync::BinComponent>();
    targetSimulator.ExitSubModel();

    // Create source bin and fill it with objects
    const EntityID sourceEntity = Ers::GetSubModel().CreateEntity("Source bin");
    auto source = Ers::GetSubModel().AddComponent<MoverModelSync::BinComponent>(sourceEntity);
    source->Stored = nObjects;

    // Create target bin and leave it empty
    targetSimulator.EnterSubModel();
    const EntityID targetEntity = Ers::GetSubModel().CreateEntity("Target bin");
    auto target = Ers::GetSubModel().AddComponent<MoverModelSync::BinComponent>(targetEntity);
    targetSimulator.ExitSubModel();

    // Create mover and set source and target to move from and to
    const EntityID moverEntity = Ers::GetSubModel().CreateEntity("Mover");
    auto mover = Ers::GetSubModel().AddComponent<MoverModelSync::MoveBehaviour>(moverEntity);
    mover->Source = sourceEntity;
    mover->Target = targetEntity;

    sourceSimulator.ExitSubModel();

    // Add source simulator as dependency to target simulator, required for sync event
    modelContainer.AddSimulatorDependency(sourceSimulator, targetSimulator);

    Ers::Logger::Info(std::format("Source bin has {} objects, Target bin has {} objects", source->Stored, target->Stored));

    Ers::Logger::Debug("Starting...");
    manager.AddModelContainer(modelContainer, endTimeForModel);

    while (manager.CountModelContainers() > 0)
    {
        manager.Update();
    }

    sourceSimulator.EnterSubModel();
    source = Ers::GetSubModel().GetComponent<MoverModelSync::BinComponent>(sourceEntity);
    sourceSimulator.ExitSubModel();
    targetSimulator.EnterSubModel();
    target = Ers::GetSubModel().GetComponent<MoverModelSync::BinComponent>(targetEntity);
    targetSimulator.ExitSubModel();
    Ers::Logger::Info(std::format("Source bin has {} objects, Target bin has {} objects", source->Stored, target->Stored));

    Ers::Uninitialize();

    return 0;
}
