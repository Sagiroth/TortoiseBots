"""Exercise production pre-shield selection with injured tanks off the chosen patient."""
import unittest
import test_soaker_preshield as preshield

class TankHealPrecedenceTest(preshield.SoakerPreshieldTest):
    extra_cases = r"""
 Player main; main.guid=4; main.assigned=true; main.hp=60; g.members.push_back(&main);
 // Main is not the selected patient: preventive shields must still yield.
 assert(!v.Calculate());
 main.hp=89; assert(!v.Calculate());
 main.hp=90; assert(v.Calculate()==&a);
 main.hp=60; main.assigned=false; main.tank=true; assert(!v.Calculate());
 main.tank=false; assert(v.Calculate()==&a); // injured DPS is not this tank guard
 main.assigned=true; main.distance=70; main.los=false; assert(!v.Calculate());
 main.combat=false; assert(v.Calculate()==&a);
 main.combat=true; main.alive=false; assert(v.Calculate()==&a);
 main.alive=true; main.same=false; assert(v.Calculate()==&a);
 main.same=true; main.friendly=false; assert(v.Calculate()==&a);
 main.friendly=true; main.hp=100;
 a.assigned=true; a.combat=true; a.hp=50; assert(!v.Calculate());
 a.hp=100; assert(v.Calculate()==&a);
 """

if __name__ == '__main__': unittest.main()
