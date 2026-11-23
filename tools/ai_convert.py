# Prevod modelu Anticipatory Music Transformer (stanford-crfm/music-small-800k) do binarneho suboru pre Korgus (f16)
import json, struct, sys, numpy as np, torch
from transformers import GPT2LMHeadModel
name = sys.argv[1] if len(sys.argv) > 1 else 'stanford-crfm/music-small-800k'
m = GPT2LMHeadModel.from_pretrained(name).eval()
c = m.config
print('config', c.n_layer, c.n_head, c.n_embd, c.vocab_size, c.n_positions)
sd = m.state_dict()
def t(k): return sd[k].detach().float().numpy()
out = open('melody.kamt', 'wb')
out.write(b'KAMT'); out.write(struct.pack('<6i', 1, c.n_layer, c.n_head, c.n_embd, c.vocab_size, c.n_positions))
def w(a): out.write(np.ascontiguousarray(a, dtype=np.float16).tobytes())
w(t('transformer.wte.weight')); w(t('transformer.wpe.weight'))
for i in range(c.n_layer):
    p = f'transformer.h.{i}.'
    for k in ['ln_1.weight','ln_1.bias','attn.c_attn.weight','attn.c_attn.bias','attn.c_proj.weight','attn.c_proj.bias',
              'ln_2.weight','ln_2.bias','mlp.c_fc.weight','mlp.c_fc.bias','mlp.c_proj.weight','mlp.c_proj.bias']:
        w(t(p + k))
w(t('transformer.ln_f.weight')); w(t('transformer.ln_f.bias'))
out.close()
# referencne hodnoty na overenie implementacie v C++
AUTOREGRESS = 55026
toks = [AUTOREGRESS]
for k in range(12):   # jednoducha stupnica C dur, klavir (nastroj 0), 30 = 0.3 s
    toks += [k * 30, 10000 + 25, 11000 + 60 + [0,2,4,5,7,9,11,12,11,9,7,5][k]]
with torch.no_grad():
    lg = m(torch.tensor([toks])).logits[0, -1].numpy().astype(float)
top = np.argsort(-lg)[:10]
json.dump({'tokens': toks, 'top': [int(x) for x in top], 'vals': [float(lg[x]) for x in top], 'l0': float(lg[0]), 'l11060': float(lg[11060])}, open('reference.json', 'w'))
print('top', top, lg[top])
