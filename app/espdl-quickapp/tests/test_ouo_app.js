// SPDX-License-Identifier: Apache-2.0
// Select the controller test for the current SD facade or an explicit SVG archive.
const fs=require('node:fs'),path=require('node:path');
process.argv[2]=process.argv[2]||path.join(__dirname,'../overlay/apps/system/desktop/ouo/app.js');
require(fs.readFileSync(process.argv[2],'utf8').includes('ui.grokSprite')?'./test_grok_sd_app.cjs':'./test_grok_ball_app.js');
