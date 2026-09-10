package web

import "embed"

// FS embeds all files in web/
//go:embed all:*
var FS embed.FS
