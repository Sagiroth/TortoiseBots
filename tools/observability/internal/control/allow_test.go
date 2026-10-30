package control

import (
	"strings"
	"testing"
)

func TestAllowList(t *testing.T) {
	accepted := map[string]string{
		".bot run for Gbmage pause":                               ".bot run for Gbmage pause",
		".bot run for Gbmage resume":                              ".bot run for Gbmage resume",
		".bot run for Gbmage skip":                                ".bot run for Gbmage skip",
		".bot run for Gbmage stop":                                ".bot run for Gbmage stop",
		".bot run for Gbmage status":                              ".bot run for Gbmage status",
		".BOT RUN FOR Gbmage PAUSE":                               ".BOT RUN FOR Gbmage PAUSE",
		"  .bot   run  for   Gbmage   pause  ":                    ".bot run for Gbmage pause",
		"bot run for Gbmage pause":                                ".bot run for Gbmage pause",
		".bot run for Gbmage start":                               ".bot run for Gbmage pause"[:0] + ".bot run for Gbmage start",
		".bot run for Gbmage start lower_karazhan_halls":          ".bot run for Gbmage start lower_karazhan_halls",
		".bot run for Gbmage start zg from 3 speed=2":             ".bot run for Gbmage start zg from 3 speed=2",
		".bot run for Gbmage start from step 12":                  ".bot run for Gbmage start from step 12",
		".bot run for Gb start molten_core from step 1 speed=3.0": ".bot run for Gb start molten_core from step 1 speed=3.0",
		".bot run for Gbmage start zg":                            ".bot run for Gbmage start zg",
		".bot run reloadplans":                                    ".bot run reloadplans",
		".BOT RUN RELOADPLANS":                                    ".BOT RUN RELOADPLANS",
		".bot gear Tanky tier 3 ilvl=66":                          ".bot gear Tanky tier 3 ilvl=66",
		".bot gear Tanky tier 3":                                  ".bot gear Tanky tier 3",
		".bot gear Tanky tier 0 ilvl=63":                          ".bot gear Tanky tier 0 ilvl=63",
		".BOT GEAR Tanky TIER 3 ILVL=66 NOENCHANTS":               ".BOT GEAR Tanky TIER 3 ILVL=66 NOENCHANTS",
		".bot gear raid Gbmage tier 2 ilvl=63 noenchants":         ".bot gear raid Gbmage tier 2 ilvl=63 noenchants",
		".bot gear Tanky preset zg":                               ".bot gear Tanky preset zg",
		".bot gear raid Gbmage preset t1":                         ".bot gear raid Gbmage preset t1",
		".bot gear raid Gbmage PRESET Kara40":                     ".bot gear raid Gbmage PRESET Kara40",
		".revive Tanky":                                           ".revive Tanky",
		".REVIVE tanky":                                           ".REVIVE tanky",
		"revive Tanky":                                            ".revive Tanky",
	}
	for in, want := range accepted {
		got, ok := Allowed(in)
		if !ok {
			t.Errorf("Allowed(%q) = refused, want %q", in, want)
			continue
		}
		if got != want {
			t.Errorf("Allowed(%q) = %q, want %q", in, got, want)
		}
	}

	refused := []string{
		".bot run for Gb pause; .server shutdown",
		".bot run for Gb start x player",
		".bot run for Gb START x PLAYER",
		".account set gmlevel x 4",
		".bot run for Gb start ../x",
		".revive Gb Bob",
		".bot gear Bob tier 9",
		".bot gear Bob tier 2 ilvl=500",
		".bot gear Bob tier 2 ilvl=0",
		".bot gear Bob preset Bad-Name!",
		".bot gear Bob preset this_preset_name_is_way_too_long",
		".bot run stop",
		"",
		"   ",
		strings.Repeat("a", 600),
		".bot commander status",
		".bot run for G pause",
		".bot run for Gbmage pause now",
		".bot run for Gb start plan;evil",
		".bot run for Gb start a|b",
		".bot run for Gb 'pause'",
		".bot run for Gb <pause>",
		".bot run for Gb pause&stop",
		".bot run for Gb start x\ny",
		".bot run for Gb1 pause",
		".bot gear B tier 2",
		".bot gear Bob tier 8",
		".bot gear Bob tier",
		".bot gear Bob tier 2 ilvl=201",
		".bot gear Bob tier 0",
		".bot gear Bob preset",
		".revive B",
		".revive",
		".bot run for Gb start zg from step 0",
		".bot run for Gb start zg from step 10000",
		".bot run for Gb start zg speed=0.9",
		".bot run for Gb start zg speed=3.1",
		".bot run for Gb start zg speed=many",
		".bot run for Gb jump",
		".bot run reloadplans now",
		".server shutdown",
		".bot run for Gb pause|resume",
	}
	for _, in := range refused {
		if got, ok := Allowed(in); ok {
			t.Errorf("Allowed(%q) = %q, want refused", in, got)
		}
	}
}
