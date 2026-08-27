import json
imgs=json.load(open('imgs.json'))
def img(n,alt,w=528,h=792):
    return f'<img src="data:image/png;base64,{imgs[n]}" alt="{alt}" width="{w}" height="{h}">'

fams=["Almendra","Coelacanth","Edgar","Inknut Junicode","Libre Franklin","Libris ADF","TeX Gyre Heros","TeX Gyre Schola"]
W=[[349.5,227.5,162.8,122.6,96.3,76.1],
   [292.6,206.7,150.2,113.7,79.7,64.6],
   [314.1,218.6,150.2,114.8,87.3,67.5],
   [298.5,190.3,156.4,110.1,81.0,62.9],
   [345.3,223.5,181.1,130.4,96.3,75.3],
   [362.9,238.1,171.1,124.2,96.5,79.2],
   [381.5,235.5,162.0,132.2,100.3,77.3],
   [312.7,210.9,147.6,112.0,84.4,66.3]]
PT=[[8,10,12,14,16,18],[9,11,13,15,18,20],[8,10,12,14,16,18],[7,9,10,12,14,16],
    [7,9,10,12,14,16],[8,10,12,14,16,18],[7,9,11,12,14,16],[8,10,12,14,16,18]]
DEV=[[6.0,2.9,2.3,3.3,4.9,6.7],[-11.3,-6.5,-5.6,-4.2,-13.2,-9.4],[-4.7,-1.1,-5.6,-3.3,-4.9,-5.5],
     [-9.5,-13.9,-1.8,-7.2,-11.7,-11.9],[4.7,1.1,13.7,9.9,4.9,5.5],[10.1,7.7,7.5,4.7,5.2,10.9],
     [15.7,6.5,1.8,11.4,9.3,8.3],[-5.2,-4.6,-7.3,-5.6,-8.1,-7.0]]
K=[1.023,0.955,0.978,0.949,1.035,1.040,1.045,0.967]
STEP=["2","2 / 3","2","1 / 2","1 / 2","2","1 / 2","2"]
SLOTS=["XXS","XS","S","M","L","XL"]
MED=[329.7,221.0,159.2,118.7,91.8,71.4]

# ---- hero: M-slot median rail
order=sorted(range(8), key=lambda i:-W[i][3])
rail=[]
for i in order:
    d=DEV[i][3]; pct=abs(d)/16*50
    side="pos" if d>0 else "neg"
    rail.append(f'''<div class="rail-row">
  <div class="rail-name">{fams[i]}</div>
  <div class="rail-track">
    <div class="rail-mid"></div>
    <div class="rail-bar {side}" style="--w:{pct:.2f}%"></div>
  </div>
  <div class="rail-num">{W[i][3]:.1f}</div>
  <div class="rail-num sec">{round(100000/W[i][3])}</div>
</div>''')
rail="\n".join(rail)

# ---- full matrix
def cell(v,d):
    cls = "over" if d>6 else ("under" if d<-6 else "")
    return f'<td class="{cls}"><span class="n">{v:.1f}</span><span class="d">{d:+.1f}%</span></td>'
rows=[]
for i in range(8):
    tds="".join(cell(W[i][s],DEV[i][s]) for s in range(6))
    rows.append(f'<tr><th scope="row">{fams[i]}</th>{tds}</tr>')
matrix="\n".join(rows)

ptrow="".join(f'<td>{"&thinsp;/&thinsp;".join(str(p) for p in [PT[i][s]])}</td>' for i in range(1) for s in range(6))

# ---- pages per 100k
p100=[]
for i in range(8):
    tds="".join(f'<td>{round(100000/W[i][s])}</td>' for s in range(6))
    p100.append(f'<tr><th scope="row">{fams[i]}</th>{tds}</tr>')
p100="\n".join(p100)

# ---- recommendation
rec=[]
for i in range(8):
    d=abs(K[i]-1)*100
    ramp=" ".join(str(x) for x in PT[i])
    even = STEP[i]=="2"
    rec.append(f'<tr><th scope="row">{fams[i]}</th><td class="mono">{K[i]:.3f}</td><td>{d:.1f}%</td>'
               f'<td class="{"" if even else "over"}">{STEP[i]}</td><td class="mono ramp">{ramp}</td></tr>')
rec="\n".join(rec)

html=open('artifact_template.html').read()
html=html.replace("{{RAIL}}",rail).replace("{{MATRIX}}",matrix).replace("{{P100}}",p100).replace("{{REC}}",rec)
html=html.replace("{{IMG_HEROS_M}}",img('page_TeXGyreHeros_3','TeX Gyre Heros, M slot, first page of the passage'))
html=html.replace("{{IMG_INKNUT_M}}",img('page_InknutJunicode_3','Inknut Junicode, M slot, first page of the passage'))
html=html.replace("{{IMG_HEROS_M2}}",img('page_HEROSfull_3','TeX Gyre Heros at k = 1.045, M slot'))
html=html.replace("{{IMG_INKNUT_M2}}",img('page_INKNUTfull_3','Inknut Junicode at k = 0.949, M slot'))
html=html.replace("{{IMG_HEROS_XXS}}",img('page_TeXGyreHeros_0','TeX Gyre Heros, XXS slot'))
html=html.replace("{{IMG_COEL_XXS}}",img('page_COELctl_0','Coelacanth, XXS slot'))
open('words-per-page.html','w').write(html)
print("wrote words-per-page.html", len(html))
