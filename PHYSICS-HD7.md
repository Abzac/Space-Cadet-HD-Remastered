# Space Cadet HD 7.0 physics

## Rebounds and flipper sweeps

Within each step, the engine finds the first contact, advances free balls to the time of impact and calculates the remaining travel using the new velocity. For the same time remaining, a rubber that slows the ball and a bumper that accelerates it now produce different travel distances. The engine also handles any subsequent contact within that time. Ball-to-ball collisions account for both velocities. When a ball enters a sink, the table mechanism takes control of its motion.

The number of intermediate flipper positions depends on the sweep angle. Equal clockwise and counterclockwise sweeps get the same number of collision checks. The game data defines the flipper geometry and the gap between the tips.

The ball has its own orientation and angular velocity about three axes. Table friction converts sliding into rolling, and sidespin changes the angle of a rebound off a rail. The fine steel grain turns with the ball. The lamp and viewer positions determine where the highlight appears.

## Collision tests

On a test surface with known geometry, the engine reproduced the calculated positions at 60, 120 and 240 steps per second. A ball that lost half its speed on hitting a wall finished at x = −0.0155000 at all three rates. Two moving balls in a head-on collision finished at −0.048 and +0.720, within 0.0000001 table units of their expected positions. In a separate test, a ball struck from rest had exactly 0.09 units of travel remaining, with the same maximum error. Tests also covered two successive impacts in one step and a ball crossing a sensor before being captured on another table level.

In gameplay tests, both side kickbacks returned the ball along their lanes. In Classic, each fired once and the second pass drained. In Meditative mode, each fired three times in succession. Each sensor crossing scored 20,000 points. The stuck-ball rescue now allows an armed kickback to complete its normal delay. Both modes were tested for an open center drain, well-timed tip hits and late swings that let the ball drain.

## How launches feel

The comparison covered five button pulls and four right-stick pulls. Each run used identical input and random seed, 120 simulation steps per second and two seconds of observation after release. The HD 6 reference matched the trace from a restored 6.3.2 build byte for byte.

Spring tension and the firing threshold matched across all 3,296 rows. Button pulls released at 5, 21, 51, 90 and 100 units of tension. Stick pulls released at 19.81, 43.05, 71.26 and 100. The stick retains the deepest pull even if it moves partway back before release.

Across the eight trials in which both versions launched the ball, first-frame launch speed changed by −6.1% to +5.4%. Plunger contact occurred on the same step or up to two steps earlier. In the ninth trial, at 51 units of tension, the ball stayed on the plunger in 6.3.2 and launched in 7.0. Within two seconds, 7.0 scored 2,000 points in three trials by reaching scoring elements. The other six matched the previous score of zero. Familiar button timings and stick positions still select the same pull strength, but a skill shot aimed at a particular lane may need a small adjustment.
