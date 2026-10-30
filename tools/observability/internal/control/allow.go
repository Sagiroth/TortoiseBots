package control

import (
	"regexp"
	"strings"
)

const maxLineLen = 512

var (
	namePat   = `[A-Za-z]{2,12}`
	planPat   = `[a-z0-9_]{1,64}`
	legPat    = `[a-zA-Z0-9_]{1,64}`
	presetPat = `[a-z0-9_]{1,24}`

	runVerbPat  = regexp.MustCompile(`(?i)^\.bot run for ` + namePat + ` (pause|resume|skip|stop|status)$`)
	revivePat   = regexp.MustCompile(`(?i)^\.revive ` + namePat + `$`)
	reloadPat   = regexp.MustCompile(`(?i)^\.bot run reloadplans$`)
	gearPat     = regexp.MustCompile(`(?i)^\.bot gear (?:raid ` + namePat + `|` + namePat + `) (?:tier ([0-7])( ilvl=([1-9][0-9]{0,2}))?( noenchants)?|preset ` + presetPat + `)$`)
	startPat    = regexp.MustCompile(`(?i)^\.bot run for ` + namePat + ` start( ` + planPat + `)?( from (?:` + legPat + `|step [1-9][0-9]{0,3}))?( speed=([0-9]+(?:\.[0-9]+)?))?$`)
	safeCharPat = regexp.MustCompile(`^[A-Za-z0-9_ .\-=+:/]+$`)
)

// Allowed reports whether line is one of the relay's commands, with the
// command normalised (single spaces, leading dot).
func Allowed(line string) (string, bool) {
	normalised := normalise(line)
	if normalised == "" || len(normalised) > maxLineLen {
		return "", false
	}
	if !safeCharPat.MatchString(normalised) {
		return "", false
	}
	if runVerbPat.MatchString(normalised) || revivePat.MatchString(normalised) || reloadPat.MatchString(normalised) {
		return normalised, true
	}
	if m := gearPat.FindStringSubmatch(normalised); m != nil {
		// Tier 0 alone gears nothing; the module needs a cap with it.
		if m[1] == "0" && m[2] == "" {
			return "", false
		}
		if m[3] != "" && (len(m[3]) > 3 || gearIlvl(m[3]) > 200) {
			return "", false
		}
		return normalised, true
	}
	m := startPat.FindStringSubmatch(normalised)
	if m == nil {
		return "", false
	}
	if strings.Contains(strings.ToLower(normalised), " player") {
		return "", false
	}
	if m[4] != "" {
		v := parseSpeed(m[4])
		if v < 1.0 || v > 3.0 {
			return "", false
		}
	}
	if strings.Contains(normalised, "from step ") && !validStep(normalised) {
		return "", false
	}
	return normalised, true
}

func normalise(line string) string {
	words := strings.Fields(line)
	if len(words) == 0 {
		return ""
	}
	out := strings.Join(words, " ")
	if !strings.HasPrefix(out, ".") {
		out = "." + out
	}
	return out
}

func gearIlvl(s string) int {
	n := 0
	for _, c := range s {
		n = n*10 + int(c-'0')
	}
	return n
}

func parseSpeed(s string) float64 {
	v := 0.0
	frac := 0.0
	div := 1.0
	after := false
	for _, c := range s {
		if c == '.' {
			after = true
			continue
		}
		if after {
			frac = frac*10 + float64(c-'0')
			div *= 10
		} else {
			v = v*10 + float64(c-'0')
		}
	}
	return v + frac/div
}

func validStep(line string) bool {
	i := strings.Index(line, "from step ")
	rest := line[i+len("from step "):]
	if end := strings.Index(rest, " "); end >= 0 {
		rest = rest[:end]
	}
	n := 0
	if rest == "" {
		return false
	}
	for _, c := range rest {
		if c < '0' || c > '9' {
			return false
		}
		n = n*10 + int(c-'0')
	}
	return n >= 1 && n <= 9999
}
