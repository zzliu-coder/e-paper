#pragma once
enum class PowerNeed {PeripheralControl};
inline int powerRefs=0;
class PowerPolicy { public: static PowerPolicy& GetInstance(){static PowerPolicy p;return p;} void Acquire(PowerNeed){++powerRefs;} void Release(PowerNeed){--powerRefs;} };
