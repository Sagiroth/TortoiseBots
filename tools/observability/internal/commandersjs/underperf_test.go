package commandersjs

import "testing"

// TestUnderperfJS runs underperf_check.js: the Fight debugger's Underperforming bots section (the window each card
// asks for, the report's escaping, null peers and fight links, the run picker's selected run, the empty and error
// states, and the card wiring the section needs).
func TestUnderperfJS(t *testing.T) {
	runCheck(t, "underperf_check.js", "../../web/static/js/underperf.js", "../../web/static/js/app.js", "../../web/static/css/underperf.css")
}
