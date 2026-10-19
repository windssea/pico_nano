/* SPDX-License-Identifier: Apache-2.0
 * 中文：从固定MIT上游生成紧凑离线黄历，不在固件中运行JavaScript。/ English: generate compact offline almanac tables from pinned MIT upstream; firmware never runs JavaScript.
 */
const fs=require('node:fs'),crypto=require('node:crypto'),path=require('node:path');
const root=path.resolve(__dirname,'..'),source=path.join(root,'tools/vendor/lunar-javascript/lunar.js');
const upstreamSha='9750324bfe1aa63c146f8c72b1143df924466c11c8a5277d7d9225c541a18aaa';
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
if(hash(fs.readFileSync(source))!==upstreamSha)throw Error('Upstream source hash mismatch');
const {Solar,LunarYear,LunarUtil}=require(source),first=1970,last=2100,base=Date.UTC(1969,0,1);
const day=s=>Math.round((Date.UTC(s.getYear(),s.getMonth()-1,s.getDay())-base)/86400000);
const months=new Map();
for(let y=1969;y<=2101;y++)for(const m of LunarYear.fromYear(y).getMonths()){
 const s=Solar.fromJulianDay(m.getFirstJulianDay()),start=day(s),month=m.getMonth();
 if(start>=0 && start<65536)months.set(start,[(m.getYear()-1969)|(Math.abs(month)<<8)|(month<0?4096:0),m.getDayCount()]);
}
const ordered=[...months].sort((a,b)=>a[0]-b[0]);
for(let i=0;i<ordered.length-1;i++)if(ordered[i+1][0]-ordered[i][0]!==ordered[i][1][1])throw Error('Discontinuous lunar month');
const starts=ordered.map(x=>x[0]),metadata=ordered.map(x=>x[1][0]);
const termNames=['小寒','大寒','立春','雨水','惊蛰','春分','清明','谷雨','立夏','小满','芒种','夏至','小暑','大暑','立秋','处暑','白露','秋分','寒露','霜降','立冬','小雪','大雪','冬至'];
let terms=[],january=[],festivalDays=[],festivalIds=[],festivalNames=[''];
for(let y=first;y<=last+1;y++){
 const l=Solar.fromYmd(y,7,1).getLunar(),q=l.getJieQiTable();
 // 同名冬至在有些上游表指前一年；所有别名均按真实公历年月筛选。/ A named winter solstice can refer to the previous year; filter all aliases by their actual Gregorian date.
 for(const name of termNames){const candidates=Object.entries(q).filter(([k,s])=>s.getYear()===y && (k===name || ({DONG_ZHI:'冬至',DA_XUE:'大雪',XIAO_HAN:'小寒',DA_HAN:'大寒',LI_CHUN:'立春',YU_SHUI:'雨水',JING_ZHE:'惊蛰'})[k]===name));if(!candidates.length)throw Error('Missing term '+y+name);terms.push(day(candidates[0][1]));}
 if(y<=last)january.push(LunarUtil.getJiaZiIndex(Solar.fromYmd(y,1,1).getLunar().getMonthInGanZhi()));
}
const pairMap=new Map(),pairIndex=[],pairOffsets=[],pairData=[];
for(const m of LunarUtil.JIA_ZI)for(const d of LunarUtil.JIA_ZI){const lists=[LunarUtil.getDayYi(m,d),LunarUtil.getDayJi(m,d)],key=JSON.stringify(lists);if(!pairMap.has(key)){pairMap.set(key,pairOffsets.length);pairOffsets.push(pairData.length);for(const list of lists){pairData.push(list.length,...list.map(x=>{const i=LunarUtil.YI_JI.indexOf(x);if(i<0 || i>255)throw Error('Activity index');return i;}));}}pairIndex.push(pairMap.get(key));}
if(pairData.length>=65536)throw Error('Activities exceed uint16 offset');
// 节日只取上游常规节日，不把其他纪念日或调休当已核定假期。/ Use regular upstream festivals, without treating other commemorations or adjusted workdays as approved holidays.
const oracle=[];let solar=Solar.fromYmd(first,1,1);while(solar.getYear()<=last){
 const l=solar.getLunar(),festivals=[...solar.getFestivals(),...l.getFestivals()],name=[...new Set(festivals)].join(' · ');if(name){let id=festivalNames.indexOf(name);if(id<0){id=festivalNames.length;festivalNames.push(name);}festivalDays.push(day(solar));festivalIds.push(id);}
 oracle.push([day(solar),l.getYear(),l.getMonth(),l.getDay(),LunarUtil.getJiaZiIndex(l.getMonthInGanZhi()),LunarUtil.getJiaZiIndex(l.getDayInGanZhi()),name,l.getDayYi().join(' '),l.getDayJi().join(' ')]);solar=solar.next(1);
}
if(festivalNames.length>255)throw Error('Festival index too wide');
const array=(type,name,values)=>`static const ${type} ${name}[]={\n${Array.from({length:Math.ceil(values.length/24)},(_,i)=>'    '+values.slice(i*24,i*24+24).join(',')+',').join('\n')}\n};\n`;
const strings=(name,values)=>`static const char *const ${name}[]={\n${values.map(x=>'    '+JSON.stringify(x)+',').join('\n')}\n};\n`;
let text='/* SPDX-License-Identifier: MIT\n * Derived from lunar-javascript, Copyright (c) 2018 6tail.\n * 中文：自动生成，勿手改；来源及许可证见LICENSES/calendar-manifest.json。\n * English: generated, do not edit; provenance and license in LICENSES/calendar-manifest.json.\n */\n';
text+=array('uint16_t','month_starts',starts)+array('uint16_t','month_metadata',metadata)+array('uint16_t','term_days',terms)+array('uint8_t','january_month_gz',january)+array('uint16_t','festival_days',festivalDays)+array('uint8_t','festival_ids',festivalIds)+strings('festival_names',festivalNames)+strings('term_names',termNames)+strings('activities',LunarUtil.YI_JI)+array('uint16_t','activity_pairs',pairIndex)+array('uint16_t','activity_offsets',pairOffsets)+array('uint8_t','activity_data',pairData);
const target=path.join(root,'components/pn_personalization/pn_chinese_calendar_data.inc');
if(process.argv.includes('--check')){if(fs.readFileSync(target,'utf8')!==text)throw Error('Generated calendar differs');}else fs.writeFileSync(target,text);
if(process.argv.includes('--oracle'))fs.writeFileSync(path.join(root,'build-dev/chinese-calendar/oracle.json'),JSON.stringify(oracle));
const yearly={};for(const row of oracle){const date=new Date(base+row[0]*86400000),y=date.getUTCFullYear();const line=[y,date.getUTCMonth()+1,date.getUTCDate(),...row.slice(1)].join('\t')+'\n';(yearly[y]??=[]).push(line);}
const fixture=JSON.stringify({sourceCommit:'4c45a59f79b856125516f31aefa8295035c16afd',yearSha256:Object.fromEntries(Object.entries(yearly).map(([y,lines])=>[y,hash(lines.join(''))]))},null,2)+'\n';
const fixturePath=path.join(root,'tests/fixtures/chinese-calendar-oracle.json');
if(process.argv.includes('--check')){if(fs.readFileSync(fixturePath,'utf8')!==fixture)throw Error('Daily oracle differs');}else fs.writeFileSync(fixturePath,fixture);
const bytes=starts.length*4+terms.length*2+january.length+festivalDays.length*3+pairIndex.length*2+pairOffsets.length*2+pairData.length+Buffer.byteLength([...festivalNames,...termNames,...LunarUtil.YI_JI].join('\0'));
console.log(JSON.stringify({days:oracle.length,months:starts.length,festivalRecords:festivalDays.length,festivalNames:festivalNames.length,activityPairs:pairOffsets.length,estimatedDataBytes:bytes,generatedSha256:hash(text)},null,2));
