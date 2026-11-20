// Desk stand for a 48 mm round ESP32-S3 knob display
// (Elecrow CrowPanel 1.28" rotary / IoTeikXgo clone, 48 x 48 x 33 mm)
//
// Screen tilted back 30 deg, cable exits out the back at desk level.
// Everything below is in millimetres.  Tweak, press F6, export STL.

/* [Device] */
dev_d     = 48;     // device outer diameter
grip_h    = 10;     // height of the NON-rotating lower body (measure yours!)
tol       = 0.6;    // diametral clearance for a snug slide-in fit

/* [Stand] */
tilt      = 30;     // degrees back from vertical
wall      = 2.4;    // cradle wall thickness
floor_t   = 3;      // cradle floor thickness
knob_gap  = 1.5;    // extra gap so the rim never touches the knob
cable_w   = 16;     // width of the cable channel
foot_w    = 60;     // footprint width  (x)
foot_d    = 66;     // footprint depth  (y)
foot_t    = 2.5;    // footprint plate thickness
cradle_y  = 6;      // cradle floor centre: forward(-)/back(+) of footprint centre
cradle_z  = 17;     // cradle floor centre height above desk

$fn = 128;

cup_h   = grip_h - knob_gap;          // actual wall height
cup_od  = dev_d + tol + 2*wall;
cup_id  = dev_d + tol;

module tilted() {                      // frame whose +Z is the cup axis
    translate([0, cradle_y, cradle_z]) rotate([tilt, 0, 0]) children();
}

module outer_body() {
    hull() {
        tilted() translate([0,0,-floor_t]) cylinder(d=cup_od, h=cup_h+floor_t);
        translate([-foot_w/2, -foot_d/2 + 4, 0]) cube([foot_w, foot_d, foot_t]);
    }
}

module cavities() {
    // device pocket (long so it opens the top of the hull)
    tilted() cylinder(d=cup_id, h=60);
    // slot through the cup floor, from axis to the rear
    tilted() translate([-cable_w/2, 0, -floor_t-1]) cube([cable_w, 40, floor_t+cup_h+2]);
    // tunnel from under the cup down and out the back at desk level
    translate([-cable_w/2, cradle_y, -1]) cube([cable_w, 60, cradle_z + 1]);
}

difference() {
    outer_body();
    cavities();
}
