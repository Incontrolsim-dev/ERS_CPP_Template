#include "Ers/Api.h"

#include "Ers/Model/ModelContainer.h"
#include "Ers/Model/ModelManager.h"
#include "Ers/Model/Simulator/Simulator.h"
#include "Ers/SubModel/EventScheduler.h"
#include "Ers/SubModel/SubModel.h"
#include "Ers/Logger.h"

#include <format>

namespace MoverModel
{
    struct BinComponent : public Ers::DataComponent
    {
        uint64_t Stored;

        bool operator==(const BinComponent& other) const { return this == &other; }
    };

    class MoveBehaviour : public Ers::ScriptBehaviorComponent
    {
      public:
        MoveBehaviour() = default;

        void OnStart();
        void OnDestroy();

        void MoveEvent();

        EntityID Source;
        EntityID Target;
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
        auto& submodel  = Ers::GetSubModel();
        auto sourceBin = submodel.GetComponent<BinComponent>(Source);
        if (sourceBin->Stored == 0)
            return; // Can't move objects if there are none

        // Move object from source bin to target bin
        sourceBin->Stored -= 1;
        auto targetBin = submodel.GetComponent<BinComponent>(Target);
        targetBin->Stored += 1;

        // Repeat MoveEvent
        const double random = submodel.GetRandomProperties().GetRandomNumberGenerator().Sample();
        SimulationTime delayTime(random * static_cast<double>(1'000'000));
        Ers::EventScheduler::ScheduleLocalEvent(0, delayTime, [this]() { MoveEvent(); });
    }
} // namespace MoverModel

int main()
{
	Ers::InitializeAPI();

    const uint64_t nObjects = 10000;
    auto endTimeForModel    = SimulationTime(10000);
    endTimeForModel *= 1'000'000; // Apply model precision

    Ers::Model::ModelManager& manager = Ers::Model::GetModelManager();
    Ers::Model::ModelContainer modelContainer = Ers::Model::ModelContainer::CreateModelContainer();

    // Create simulator and get submodel
    auto simulator = modelContainer.AddSimulator("Simulator 1", Ers::SimulatorType::DiscreteEvent);
    auto& submodel = simulator.GetSubModel();

    // Register types
    submodel.AddComponentType<MoverModel::BinComponent>();
    submodel.AddComponentType<MoverModel::MoveBehaviour>();

    // Create source bin and fill it with objects
    const EntityID sourceEntity = submodel.CreateEntity("Source bin");
    auto source                 = submodel.AddComponent<MoverModel::BinComponent>(sourceEntity);
    source->Stored              = nObjects;

    // Create target bin and leave it empty
    const EntityID targetEntity = submodel.CreateEntity("Target bin");
    auto target                 = submodel.AddComponent<MoverModel::BinComponent>(targetEntity);
    target->Stored              = 0;

    // Create mover and set source and target to move from and to
    const EntityID moverEntity = submodel.CreateEntity("Mover");
    auto mover                 = submodel.AddComponent<MoverModel::MoveBehaviour>(moverEntity);
    mover->Source              = sourceEntity;
    mover->Target              = targetEntity;

    Ers::Logger::Info(std::format("Source bin has {} objects, Target bin has {} objects", source->Stored, target->Stored));

    Ers::Logger::Debug("Starting...");
    manager.AddModelContainer(modelContainer, endTimeForModel);

    while (manager.CountModelContainers() > 0)
    {
        manager.Update();
    }

    auto sourceResult = submodel.GetComponent<MoverModel::BinComponent>(sourceEntity);
    auto targetResult = submodel.GetComponent<MoverModel::BinComponent>(targetEntity);
    Ers::Logger::Info(std::format("Source bin has {} objects, Target bin has {} objects", sourceResult->Stored, targetResult->Stored));
	return 0;
}
