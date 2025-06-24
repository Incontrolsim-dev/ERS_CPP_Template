# Mover model sync

This model models two bins and a mover, across two simulators and sub models.
The first bin is the source bin, the second bin is the target bin.
The first bin and the mover are in the "Source Simulator" simulator, but the target bin is in the "Target Simulator" simulator. Entities are transferred between the simulators via sync events.

The mover decreases the value of `Stored` of the source bin, and increases the value of `Stored` in the target bin.
