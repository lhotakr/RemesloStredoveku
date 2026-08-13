const fs = require('fs');
const path = require('path');
const sharp = require('sharp');

const root = __dirname;
const out = path.join(root, 'mockups');
fs.mkdirSync(out, {recursive: true});
const bg64 = fs.readFileSync(path.join(root, 'assets', 'smithy-style-reference.png')).toString('base64');

const phaseNames = ['Materiál','Výheň','Kovadlina','Kalení','Broušení','Sestavení','Zkouška'];
const stations = [
 {id:'forge',name:'VÝHEŇ',sub:'Čtení žáru',phase:1,work:'Polotovar sekery',stage:'místní ohřev ostří',instruction:'Zahřej pracovní část rovnoměrně a včas ji vytáhni.',tool:'Kleště • měchy',senses:['BARVA','JISKRY','ZVUK'],obs:[['Barva','jasně oranžová','good'],['Ohřev','levá část chladnější','warn'],['Okuje','lehké','good'],['Jiskření','zatím žádné','good']],note:['Teplota zůstává skrytá.','Rozhoduje barva, okuje,','jiskření a zvuk výhně.'],quote:['„Nežeň měchy bez rozmyslu. Ostří už pracuje,','ale u oka je železo ještě tmavé.“'],controls:[['A / D','posunout'],['Q / E','otočit'],['MEZERNÍK','měchy'],['ENTER','vytáhnout']]},
 {id:'anvil',name:'KOVADLINA',sub:'Tvarování údery',phase:2,work:'Polotovar sekery',stage:'vytahování ostří',instruction:'Rozšiř ostří k cílovému obrysu a udrž osu výrobku.',tool:'Plochá strana kladiva',senses:['TVAR','ŽÁR','ZVUK'],obs:[['Žár','vhodný k práci','good'],['Osa','nepatrně utíká doprava','warn'],['Tloušťka','střed je příliš silný','warn'],['Povrch','bez prasklin','good']],note:['Cílový obrys je učňovská','pomůcka. S praxí zeslábne','a později zmizí.'],quote:['„Dobrá rána má směr. Výrobek obracej dřív,','než ti uteče osa.“'],controls:[['MYŠ','místo úderu'],['LMB','síla'],['RMB','obrátit'],['KOLEČKO','nástroj']]},
 {id:'quench',name:'KALICÍ KÁĎ',sub:'Kalení ostří',phase:3,work:'Sekera',stage:'kalení pracovní hrany',instruction:'Ponoř pouze ostří, drž sklon a nereaguj zbrkle na páru.',tool:'Kleště • voda',senses:['PÁRA','CHVĚNÍ','ZVUK'],obs:[['Poloha','lehce nakloněná','warn'],['Hloubka','kalí se jen ostří','good'],['Pára','silná a souvislá','good'],['Chvění','mírné','good']],note:['Čas neodpočítává číslo.','Hráč čte páru, syčení','a odpor v kleštích.'],quote:['„Klidně. Neviklej s tím. Když ponoříš i oko,','uděláš křehkým, co má zůstat houževnaté.“'],controls:[['MYŠ SVISLE','ponořit'],['MYŠ STRANOU','náklon'],['LMB','držet'],['ENTER','vyjmout']]},
 {id:'grind',name:'BRUSNÝ KÁMEN',sub:'Ostření a chlazení',phase:4,work:'Sekera',stage:'tvorba ostří',instruction:'Drž stálý úhel, pohybuj ostřím a nenech je přehřát.',tool:'Brusný kámen • voda',senses:['JISKRY','ZVUK','ODLESK'],obs:[['Úhel','stálý','good'],['Jiskry','jemné a krátké','good'],['Odlesk','pravá strana širší','warn'],['Žár ostří','začíná hřát','warn']],note:['Barva okraje a délka jisker','nahrazují moderní ukazatel','přehřátí.'],quote:['„Netlač na jedno místo. Veď celé ostří po kameni,','jinak v něm vybrousíš důlek.“'],controls:[['MYŠ','vést ostří'],['LMB','přitlačit'],['S / W','úhel'],['C','ochladit']]},
 {id:'bench',name:'PRACOVNÍ STŮL',sub:'Nasazení topůrka',phase:5,work:'Sekera a topůrko',stage:'sestavení',instruction:'Usaď topůrko těsně, srovnej osu a zatluč dřevěný klín.',tool:'Poříz • palička • klín',senses:['VŮLE','OSA','DŘEVO'],obs:[['Uložení','těsné','good'],['Osa','hlava míří mírně vlevo','warn'],['Topůrko','bez prasklin','good'],['Klín','ještě není usazen','warn']],note:['Odebrané dřevo se nevrátí.','Hráč ubírá vysoká místa','postupně.'],quote:['„Nespěchej s klínem. Nejdřív srovnej hlavu','s topůrkem, potom všechno zajisti.“'],controls:[['MYŠ','označit'],['LMB','ubrat dřevo'],['RMB','zkusit nasadit'],['ENTER','klín']]},
 {id:'test',name:'ZKOUŠKA',sub:'Funkční ověření',phase:6,work:'Hotová sekera',stage:'zkouška na polenu',instruction:'Prohlédni výrobek, proveď úder a rozhodni, zda obstál.',tool:'Hotový výrobek',senses:['ZVUK','RÁZ','STOPA'],obs:[['Záběr','čistý, ale mělký','warn'],['Topůrko','bez pohybu','good'],['Osa úderu','mírně vlevo','warn'],['Ostří','bez vyštípnutí','good']],note:['Jakost se ukáže až při práci.','Kus lze přijmout, opravit','nebo vyřadit.'],quote:['„Ostří drží a topůrko sedí. Jen hlava táhne','doleva — mistrovský kus to není.“'],controls:[['ALT','prohlédnout'],['LMB','zkouška'],['R','opravit'],['ENTER','přijmout']]}
];

const esc = s => String(s).replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&apos;'}[c]));
const txt = (x,y,t,size=20,fill='#eadfc8',anchor='start',extra='') => `<text x="${x}" y="${y}" font-family="DejaVu Serif,serif" font-size="${size}" fill="${fill}" text-anchor="${anchor}" ${extra}>${esc(t)}</text>`;
const lineText = (x,y,lines,size,fill,dy=28,extra='') => lines.map((t,i)=>txt(x,y+i*dy,t,size,fill,'start',extra)).join('');
const panel = (x,y,w,h,kind='dark') => kind==='parch'
 ? `<rect x="${x}" y="${y}" width="${w}" height="${h}" rx="5" fill="url(#parch)" stroke="#251a10" stroke-width="4"/><rect x="${x+6}" y="${y+6}" width="${w-12}" height="${h-12}" rx="3" fill="none" stroke="#765b3b" stroke-width="2"/>`
 : `<rect x="${x}" y="${y}" width="${w}" height="${h}" rx="5" fill="url(#wood)" stroke="#0d0b09" stroke-width="5"/><rect x="${x+7}" y="${y+7}" width="${w-14}" height="${h-14}" rx="3" fill="none" stroke="#76695b" stroke-width="2"/>`;
const rivets = (x,y,w,h) => [[x+12,y+12],[x+w-12,y+12],[x+12,y+h-12],[x+w-12,y+h-12]].map(p=>`<circle cx="${p[0]}" cy="${p[1]}" r="6" fill="url(#rivet)"/>`).join('');

function scene(id){
 if(id==='forge') return `<ellipse cx="770" cy="525" rx="455" ry="145" fill="#120b07" stroke="#4b2818" stroke-width="12"/><ellipse cx="770" cy="515" rx="365" ry="95" fill="url(#coal)" filter="url(#glow)"/><path d="M405 470 L1070 442 L1225 490 L1055 525 L405 535 Z" fill="url(#hot)" filter="url(#glow)"/><path d="M190 605 Q115 490 218 385 Q300 480 290 615 Z" fill="url(#wood)" stroke="#1d130d" stroke-width="10"/><path d="M275 485 L430 505 L430 529 L276 517 Z" fill="#4d4640"/><path d="M445 620 Q530 545 620 602" fill="none" stroke="#9fbfc4" stroke-width="7" opacity=".7"/><g font-family="DejaVu Serif" font-size="18" fill="#ddc7a3" text-anchor="middle"><text x="545" y="635">chladnější</text><text x="780" y="635" fill="#ffd18a">pracovní žár</text><text x="1020" y="635">klesající žár</text></g>`;
 if(id==='anvil') return `<path d="M300 525 L910 492 L1215 548 L1080 590 L985 710 L455 710 L382 608 L298 585 Z" fill="url(#iron)" stroke="#111" stroke-width="10"/><path d="M435 500 L1035 468 L1160 516 L1025 548 L432 564 Z" fill="url(#hot)" filter="url(#glow)"/><path d="M420 475 L1040 442 L1190 512 L1038 578 L420 590 Z" fill="none" stroke="#e2d6bd" stroke-width="4" stroke-dasharray="14 12" opacity=".65"/><circle cx="790" cy="505" r="52" fill="none" stroke="#ffca79" stroke-width="7" filter="url(#glow)"/><path d="M790 435 V575 M720 505 H860" stroke="#ffca79" stroke-width="5"/><path d="M1040 250 L1250 355 L1220 420 L1010 308 Z" fill="url(#iron)" stroke="#15120f" stroke-width="7"/><path d="M995 260 L1080 235 L1110 300 L1028 326 Z" fill="#2d2a27" stroke="#111" stroke-width="7"/>`;
 if(id==='quench') return `<path d="M450 435 Q455 730 610 760 H930 Q1090 730 1095 435 Z" fill="url(#wood)" stroke="#20140d" stroke-width="16"/><ellipse cx="772" cy="438" rx="305" ry="105" fill="url(#water)" stroke="#19130e" stroke-width="12"/><path d="M732 180 L815 180 L830 600 L705 600 Z" fill="url(#hot)" filter="url(#glow)" transform="rotate(8 772 600)"/><path d="M772 140 L772 670" stroke="#e8d9b6" stroke-width="4" stroke-dasharray="14 10" opacity=".55"/><g filter="url(#blur)" fill="#e9eee8" opacity=".55"><circle cx="680" cy="360" r="95"/><circle cx="795" cy="320" r="115"/><circle cx="905" cy="380" r="80"/></g>`;
 if(id==='grind') return `<circle cx="650" cy="505" r="255" fill="#2b2723" stroke="#756b60" stroke-width="42"/><circle cx="650" cy="505" r="190" fill="none" stroke="#978d83" stroke-width="4" stroke-dasharray="14 15" opacity=".5"/><circle cx="650" cy="505" r="52" fill="#191613" stroke="#8e8174" stroke-width="13"/><path d="M780 450 L1215 375 L1350 440 L1220 510 L785 528 Z" fill="url(#steel)" stroke="#171513" stroke-width="8"/><g stroke="#ffa54e" stroke-width="6" filter="url(#glow)"><path d="M805 475 L1120 600"/><path d="M820 488 L1060 670"/><path d="M835 500 L1185 620"/><path d="M800 463 L1220 550"/></g>`;
 if(id==='bench') return `<rect x="180" y="470" width="1120" height="250" rx="18" fill="url(#wood)" stroke="#1b110b" stroke-width="14"/><path d="M710 320 Q752 300 792 325 L825 775 Q775 825 725 780 Z" fill="url(#handle)" stroke="#2c1b11" stroke-width="7" transform="rotate(-5 770 780)"/><path d="M560 290 L790 245 L970 340 L900 500 L735 565 L555 470 L485 365 Z" fill="url(#iron)" stroke="#171513" stroke-width="10"/><ellipse cx="764" cy="477" rx="92" ry="60" fill="none" stroke="#ffc46f" stroke-width="7" stroke-dasharray="13 9" filter="url(#glow)"/>`;
 return `<rect x="285" y="505" width="790" height="225" rx="110" fill="url(#log)" stroke="#1b110b" stroke-width="13"/><path d="M795 180 Q825 160 855 185 L925 760 Q875 795 825 765 Z" fill="url(#handle)" stroke="#28180f" stroke-width="8" transform="rotate(-24 875 760)"/><path d="M650 180 L850 120 L1010 225 L930 370 L750 405 L610 310 Z" fill="url(#iron)" stroke="#151312" stroke-width="10" transform="rotate(-24 875 760)"/><circle cx="890" cy="505" r="42" fill="#ffae58" filter="url(#glow)"/><g stroke="#ffbd68" stroke-width="6"><path d="M890 420 V360"/><path d="M955 445 L1005 395"/><path d="M980 505 H1055"/><path d="M825 445 L775 395"/></g>`;
}

function svg(s, index){
 const obsColors={good:'#395034',warn:'#815c22',bad:'#842f28'};
 const tabs=['Výheň','Kovadlina','Káď','Brus','Stůl','Zkouška'];
 return `<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="1920" height="1080" viewBox="0 0 1920 1080">
<defs>
 <linearGradient id="wood" x1="0" x2="1"><stop stop-color="#1a100a"/><stop offset=".3" stop-color="#684128"/><stop offset=".65" stop-color="#3a2316"/><stop offset="1" stop-color="#17100b"/></linearGradient>
 <linearGradient id="parch" x1="0" y1="0" x2="1" y2="1"><stop stop-color="#e1cea8"/><stop offset=".58" stop-color="#cdb487"/><stop offset="1" stop-color="#b79a6b"/></linearGradient>
 <linearGradient id="iron" x1="0" y1="0" x2="0" y2="1"><stop stop-color="#89827a"/><stop offset=".38" stop-color="#3c3936"/><stop offset="1" stop-color="#171615"/></linearGradient>
 <linearGradient id="steel" x1="0" y1="0" x2="0" y2="1"><stop stop-color="#bdc5c4"/><stop offset=".5" stop-color="#555b59"/><stop offset="1" stop-color="#202220"/></linearGradient>
 <linearGradient id="hot" x1="0" x2="1"><stop stop-color="#5c342a"/><stop offset=".24" stop-color="#a74426"/><stop offset=".53" stop-color="#ff8236"/><stop offset=".67" stop-color="#ffd16c"/><stop offset=".86" stop-color="#d15a2b"/><stop offset="1" stop-color="#513127"/></linearGradient>
 <linearGradient id="handle" x1="0" x2="1"><stop stop-color="#4b2b1b"/><stop offset=".5" stop-color="#b47f4d"/><stop offset="1" stop-color="#57331f"/></linearGradient>
 <linearGradient id="water" x1="0" y1="0" x2="0" y2="1"><stop stop-color="#93adaa"/><stop offset=".5" stop-color="#42666a"/><stop offset="1" stop-color="#18383d"/></linearGradient>
 <radialGradient id="coal"><stop stop-color="#ffd26f"/><stop offset=".13" stop-color="#ff742d"/><stop offset=".3" stop-color="#832c17"/><stop offset=".72" stop-color="#25110b"/><stop offset="1" stop-color="#0c0907"/></radialGradient>
 <linearGradient id="log" x1="0" y1="0" x2="0" y2="1"><stop stop-color="#865333"/><stop offset=".45" stop-color="#3c2518"/><stop offset="1" stop-color="#1d120c"/></linearGradient>
 <radialGradient id="rivet"><stop stop-color="#8d8274"/><stop offset=".45" stop-color="#3d3934"/><stop offset="1" stop-color="#0c0b0a"/></radialGradient>
 <filter id="glow"><feGaussianBlur stdDeviation="8" result="b"/><feMerge><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge></filter>
 <filter id="blur"><feGaussianBlur stdDeviation="18"/></filter>
</defs>
<image href="data:image/png;base64,${bg64}" x="0" y="0" width="1920" height="1080" preserveAspectRatio="xMidYMid slice" opacity=".55"/>
<rect width="1920" height="1080" fill="#090604" opacity=".32"/><rect x="0" y="0" width="1920" height="1080" fill="none" stroke="#070504" stroke-width="28"/>
${panel(28,24,390,116,'parch')}${rivets(28,24,390,116)}
${txt(64,52,'ŘEMESLO STŘEDOVĚKU',17,'#786046','start','letter-spacing="3"')}${txt(64,96,s.name,34,'#21160e','start','font-weight="700"')}${txt(64,124,s.sub,18,'#58412d')}
${panel(440,26,1042,104)}${rivets(440,26,1042,104)}
${phaseNames.map((p,i)=>{const x=500+i*142;const active=i===s.phase,done=i<s.phase;return `<circle cx="${x}" cy="63" r="16" fill="${active?'#f07a31':done?'#667e56':'#171411'}" stroke="${active?'#ffd185':done?'#9db18c':'#716456'}" stroke-width="3" ${active?'filter="url(#glow)"':''}/>${i<6?`<path d="M${x+20} 63 H${x+122}" stroke="#685d51" stroke-width="3"/>`:''}${txt(x,103,p,14,active?'#ffe0aa':done?'#d5c9b5':'#8f8374','middle')}`}).join('')}
<rect x="28" y="156" width="1464" height="675" fill="#080604" opacity=".62" stroke="#19120d" stroke-width="7"/>
${s.senses.map((x,i)=>`<rect x="${52+i*108}" y="177" width="96" height="34" fill="#17110d" stroke="#a1703e"/>${txt(100+i*108,200,x,13,'#ffd09a','middle')}`).join('')}
<rect x="405" y="174" width="720" height="50" fill="#110d09" opacity=".94" stroke="#87684a"/>${txt(765,207,s.instruction,21,'#f0e1c5','middle')}
<rect x="1198" y="174" width="268" height="44" fill="#110d09" opacity=".94" stroke="#68513a"/>${txt(1332,202,s.tool,16,'#d6c6ad','middle')}
${scene(s.id)}
${panel(1520,24,372,805,'parch')}${rivets(1520,24,372,805)}${txt(1706,62,'CO PRÁVĚ POZORUJI',18,'#755a39','middle','letter-spacing="2"')}
<path d="M1550 78 H1862" stroke="#866941"/>
<rect x="1542" y="96" width="328" height="146" fill="#c6ac7c" stroke="#64492e" stroke-width="2"/>${txt(1560,126,s.work,23,'#2a1b10','start','font-weight="700"')}${txt(1560,151,s.stage,16,'#60472f')}
<rect x="1570" y="170" width="272" height="52" fill="#2a211a" stroke="#4e3825"/> <path d="M1590 187 L1770 178 L1828 197 L1768 210 L1590 214 Z" fill="url(#hot)"/>
${s.obs.map((o,i)=>{const y=279+i*88;return `<rect x="1550" y="${y}" width="306" height="74" fill="none" stroke="#846944" stroke-width="1"/><rect x="1562" y="${y+18}" width="28" height="28" transform="rotate(45 ${1576} ${y+32})" fill="#3b2a1d" stroke="#6c5032"/>${txt(1605,y+25,o[0].toUpperCase(),14,'#725739','start','letter-spacing="1.5"')}${txt(1605,y+52,o[1],20,obsColors[o[2]])}`}).join('')}
<rect x="1542" y="655" width="328" height="150" fill="#c6ac7c" stroke="#654a30"/>${lineText(1560,686,s.note,17,'#3e2a19',27)}
${panel(28,847,880,133,'parch')}${rivets(28,847,880,133)}<circle cx="92" cy="913" r="45" fill="#39271c" stroke="#6d5238" stroke-width="4"/>${txt(92,927,'B',34,'#e1c391','middle','font-weight="700"')}${txt(158,880,'BENEŠ, KOVÁŘ',14,'#71563a','start','letter-spacing="3"')}${lineText(158,915,s.quote,21,'#2c1d12',28,'font-style="italic"')}
${panel(1520,847,372,133)}${tabs.map((t,i)=>{const x=1532+i*58;const active=i===index;return `<rect x="${x}" y="865" width="52" height="82" rx="3" fill="${active?'#3a281b':'#1c1916'}" stroke="${active?'#d08b47':'#65584a'}" stroke-width="2"/>${txt(x+26,885,String(i+1),12,'#87796a','middle')}${txt(x+26,914,['♨','⚒','≋','◉','⌁','✦'][i],22,active?'#ffd69c':'#a59783','middle')}${txt(x+26,938,t,10,active?'#ffd69c':'#a59783','middle')}`}).join('')}
${panel(28,990,1864,72)}${rivets(28,990,1864,72)}
${s.controls.map((c,i)=>{const x=65+i*320;return `<rect x="${x}" y="1007" width="120" height="38" rx="4" fill="#26221e" stroke="#776b5f" stroke-width="2"/>${txt(x+60,1032,c[0],14,'#f2dbb7','middle','font-weight="700"')}${txt(x+136,1032,c[1],16,'#d6c8b3')}`}).join('')}
<rect x="1660" y="1007" width="65" height="38" rx="4" fill="#26221e" stroke="#776b5f" stroke-width="2"/>${txt(1692,1032,'ESC',14,'#f2dbb7','middle','font-weight="700"')}${txt(1740,1032,'odejít',16,'#bcae99')}
</svg>`;
}

(async()=>{
  const thumbs=[];
  for(let i=0;i<stations.length;i++){
    const data=svg(stations[i],i);
    const svgPath=path.join(out,`smithing_ui_${i+1}_${stations[i].id}.svg`);
    const pngPath=path.join(out,`smithing_ui_${i+1}_${stations[i].id}.png`);
    const portableSvg = data.replace(`data:image/png;base64,${bg64}`, '../assets/smithy-style-reference.png');
    fs.writeFileSync(svgPath,portableSvg);
    await sharp(Buffer.from(data)).png().toFile(pngPath);
    const thumb=await sharp(pngPath).resize(760,428).toBuffer();
    thumbs.push({input:thumb,left:(i%2)*790,top:Math.floor(i/2)*458});
  }
  const sheet=sharp({create:{width:1580,height:1374,channels:4,background:'#100b08'}}).composite(thumbs);
  await sheet.png().toFile(path.join(out,'smithing_ui_overview.png'));
})();
