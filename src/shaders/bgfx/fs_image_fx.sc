// license:GPLv3+

$input v_worldPos, v_tablePos, v_normal, v_texcoord0
#ifdef STEREO
	$input v_eye
#endif

#include "common.sh"

uniform vec4 staticColor_Alpha;
uniform vec4 effectParams[2];

SAMPLER2D(tex_base_color, 0);

vec4 fx_tex(vec2 p)
{
	return texture2D(tex_base_color, vec2(p.x, 1.0 - p.y));
}

float fx_hash(vec2 p)
{
	return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

float fx_noise(vec2 p)
{
	vec2 i = floor(p);
	vec2 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	float a = fx_hash(i);
	float b = fx_hash(i + vec2(1.0, 0.0));
	float c = fx_hash(i + vec2(0.0, 1.0));
	float d = fx_hash(i + vec2(1.0, 1.0));
	return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float fx_fbm(vec2 p)
{
	float v = 0.0;
	float a = 0.5;
	for (int i = 0; i < 4; i++)
	{
		v += a * fx_noise(p);
		p *= 2.0;
		a *= 0.5;
	}
	return v;
}

vec3 fx_apply(vec2 tc, int effect, float uTime, float uIntensity, vec4 original)
{
	vec2 uv = vec2(tc.x, 1.0 - tc.y);
	vec3 color = original.rgb;
	float t = uTime;

	if (effect == 1)
	{
		vec2 smokeUV = uv * 3.0 + vec2(t * 0.15, t * 0.08);
		float smoke = fx_fbm(smokeUV);
		smoke += fx_fbm(smokeUV * 2.0 - t * 0.2) * 0.5;
		smoke = smoothstep(0.3, 0.8, smoke) * 0.6 * uIntensity;
		color = mix(color, vec3(0.9, 0.92, 1.0), smoke);
	}
	else if (effect == 2)
	{
		float te = uTime * 4.0;
		vec2 toCenter = uv - vec2(0.5, 0.5);
		float dist = length(toCenter);
		vec2 dir = normalize(toCenter + 0.0001);
		float ringPos = mod(te * 1.5, 1.5);
		float ring = abs(dist - ringPos);
		float shockwave = smoothstep(0.15, 0.0, ring) * (1.0 - dist * 0.5);
		float pulse = sin(te * 0.7) * 0.5 + 0.5;
		pulse = pow(pulse, 4.0);
		float radial = smoothstep(1.0, 0.0, dist * 1.2);
		float flash = pulse * radial * uIntensity;
		float distortAmt = shockwave * 0.08 + flash * 0.03;
		vec2 distort = dir * distortAmt;
		float aberration = flash * 0.025;
		color.r = fx_tex(uv + distort + dir * aberration).r;
		color.g = fx_tex(uv + distort).g;
		color.b = fx_tex(uv + distort - dir * aberration * 0.5).b;
		color = mix(color, vec3(1.0, 0.4, 0.05), flash * 0.6);
		color += vec3(1.0, 0.95, 0.7) * flash * 1.5;
		color += vec3(0.8, 0.7, 0.5) * shockwave * uIntensity;
		color *= 1.0 + flash * 1.5;
	}
	else if (effect == 3)
	{
		float wave = sin(uv.y * 20.0 + t * 5.0) * 0.002 * uIntensity;
		wave += sin(uv.y * 35.0 - t * 3.0) * 0.001 * uIntensity;
		color = fx_tex(uv + vec2(wave, 0.0)).rgb;
	}
	else if (effect == 4)
	{
		float grain = fx_hash(uv + t) * 0.1 * uIntensity;
		color += grain;
		float vig = smoothstep(1.0, 0.3, length(uv - 0.5));
		color *= mix(1.0, vig, uIntensity);
	}
	else if (effect == 5)
	{
		float fadeOut;
		float tf;
		if (uTime > 1.0)
		{
			fadeOut = 1.0;
			tf = uTime * 3.0;
		}
		else
		{
			float progress = min(uTime, 1.0);
			fadeOut = 1.0 - smoothstep(0.7, 1.0, progress);
			tf = progress * 3.0;
		}
		float mask = 1.0 - smoothstep(0.0, uIntensity * 0.7, uv.y);
		if (mask > 0.0 && fadeOut > 0.0)
		{
			vec2 q = uv;
			q.x *= 3.0;
			q.y -= tf * 1.2;
			float f = fx_fbm(q * 2.0 + vec2(0.0, -tf * 0.4));
			f += fx_fbm(q * 4.0) * 0.5;
			f = pow(f * mask * fadeOut, 2.0);
			f = smoothstep(0.3, 0.9, f);
			vec3 fire = mix(vec3(0.8, 0.0, 0.0), vec3(1.0, 0.5, 0.0), smoothstep(0.0, 0.5, f));
			fire = mix(fire, vec3(1.0, 0.9, 0.2), smoothstep(0.4, 1.0, f));
			color += fire * f * uIntensity;
		}
	}
	else if (effect == 6)
	{
		float duration = 100.0;
		float burstSize = uIntensity;
		vec3 fireworks = vec3(0.0, 0.0, 0.0);
		float spawnRate = 0.7;
		float shellLife = 3.5;
		float latestShell = floor(t / spawnRate);
		for (float k = 0.0; k < 8.0; k += 1.0)
		{
			float shellIdx = latestShell - k;
			if (shellIdx < 0.0) continue;
			float spawnTime = shellIdx * spawnRate;
			if (duration > 0.0 && spawnTime > duration) continue;
			float shellAge = t - spawnTime;
			if (shellAge > shellLife) continue;
			float seed = shellIdx + 0.5;
			float bx = 0.15 + fx_hash(vec2(seed, 1.0)) * 0.7;
			float by = 0.4 + fx_hash(vec2(seed, 2.0)) * 0.35;
			vec2 burstCenter = vec2(bx, by);
			float riseTime = 0.4;
			if (shellAge < riseTime)
			{
				float rf = shellAge / riseTime;
				vec2 risePos = mix(vec2(bx, -0.05), burstCenter, rf * rf);
				float hd = length(uv - risePos);
				fireworks += vec3(1.0, 0.9, 0.7) * 0.005 / (hd + 0.001);
				for (float tr = 1.0; tr < 8.0; tr += 1.0)
				{
					float trf = rf - tr * 0.015;
					if (trf < 0.0) continue;
					vec2 tp = mix(vec2(bx, -0.05), burstCenter, trf * trf);
					tp.x += sin(trf * 40.0) * 0.003;
					float td = length(uv - tp);
					fireworks += vec3(1.0, 0.7, 0.3) * (1.0 - tr / 8.0) * 0.002 / (td + 0.002);
				}
				continue;
			}
			float burstAge = shellAge - riseTime;
			float hueA = fx_hash(vec2(seed, 4.0)) * 6.28318;
			for (float b = 0.0; b < 3.0; b += 1.0)
			{
				float bAge = burstAge - b * 0.08;
				if (bAge < 0.0 || bAge > 2.5) continue;
				vec2 bOff = vec2(fx_hash(vec2(seed, b * 10.0 + 20.0)) - 0.5, fx_hash(vec2(seed, b * 10.0 + 21.0)) - 0.5) * 0.03;
				vec2 bCenter = burstCenter + bOff;
				float fd = length(uv - bCenter);
				fireworks += vec3(1.0, 1.0, 0.9) * exp(-bAge * 15.0) * 0.01 / (fd + 0.002);
				float hShift = (b - 1.0) * 0.8;
				vec3 bColor = vec3(0.55 + 0.45 * cos(hueA + hShift), 0.55 + 0.45 * cos(hueA + hShift + 2.094), 0.55 + 0.45 * cos(hueA + hShift + 4.188));
				float lifeFrac = bAge / 2.5;
				float fade = (1.0 - lifeFrac) * (1.0 - lifeFrac);
				float angleOff = b * 0.52;
				for (float p = 0.0; p < 12.0; p += 1.0)
				{
					float angle = p * 2.39996 + angleOff + fx_hash(vec2(seed, p + b * 100.0)) * 0.3;
					float speed = burstSize * (0.8 + fx_hash(vec2(seed, p + b * 100.0 + 50.0)) * 0.4);
					vec2 pdir = vec2(cos(angle), sin(angle));
					float drag = 2.0;
					float grav = 0.18;
					float dragF = (1.0 - exp(-drag * bAge)) / drag;
					vec2 pPos = bCenter + pdir * speed * dragF;
					pPos.y -= grav * bAge * bAge;
					float twinkle = 0.6 + 0.4 * sin(bAge * 25.0 + p * 5.3);
					float pDist = length(uv - pPos);
					float glow = 0.0025 / (pDist + 0.0015) * fade * twinkle;
					vec3 pColor = mix(bColor, vec3(1.0, 0.35, 0.05), lifeFrac * 0.7);
					fireworks += pColor * glow;
					for (float tt = 1.0; tt < 3.0; tt += 1.0)
					{
						float tTime = bAge - tt * 0.025;
						if (tTime < 0.0) continue;
						float tDragF = (1.0 - exp(-drag * tTime)) / drag;
						vec2 tPos = bCenter + pdir * speed * tDragF;
						tPos.y -= grav * tTime * tTime;
						float tDist = length(uv - tPos);
						fireworks += pColor * 0.0012 / (tDist + 0.002) * fade * (1.0 - tt / 3.0) * 0.4;
					}
				}
			}
		}
		color += clamp(fireworks, 0.0, 1.0);
	}
	else if (effect == 7)
	{
		float str = uIntensity * 0.03;
		float sx = sin(uTime * 73.1) * cos(uTime * 43.7) * str;
		float sy = sin(uTime * 57.3) * cos(uTime * 89.3) * str;
		color = fx_tex(clamp(uv + vec2(sx, sy), 0.0, 1.0)).rgb;
	}
	else if (effect == 8)
	{
		float strikeRate = 1.2;
		float strikeLife = 0.8;
		float latestStrike = floor(t / strikeRate);
		vec3 lt = vec3(0.0, 0.0, 0.0);
		for (float k = 0.0; k < 3.0; k += 1.0)
		{
			float si = latestStrike - k;
			if (si < 0.0) continue;
			float age = t - si * strikeRate;
			if (age > strikeLife) continue;
			float sd = si + 0.5;
			float flash1 = exp(-age * 18.0);
			float flash2 = exp(-(age - 0.08) * 25.0) * step(0.08, age) * 0.5;
			float flash3 = exp(-(age - 0.2) * 30.0) * step(0.2, age) * 0.3;
			lt += vec3(0.7, 0.75, 1.0) * (flash1 + flash2 + flash3) * uIntensity * 0.25;
			float boltFade = exp(-age * 5.0);
			float flicker = step(0.08, age) * exp(-(age - 0.08) * 15.0) * 0.6;
			float bv = (boltFade + flicker) * uIntensity;
			if (bv < 0.01) continue;
			float boltX = 0.15 + fx_hash(vec2(sd, 1.0)) * 0.7;
			float endY = 0.05 + fx_hash(vec2(sd, 2.0)) * 0.3;
			float numBr = 2.0 + floor(fx_hash(vec2(sd, 3.0)) * 2.0);
			for (float br = 0.0; br < 4.0; br += 1.0)
			{
				if (br >= numBr) break;
				float bs = sd + br * 7.0;
				float startY = 1.0;
				float fEndY = endY;
				float xw = boltX;
				float bw = 1.0;
				float bb = 1.0;
				if (br > 0.0)
				{
					startY = 0.4 + fx_hash(vec2(bs, 10.0)) * 0.4;
					fEndY = startY - 0.15 - fx_hash(vec2(bs, 11.0)) * 0.2;
					for (float fs = 0.0; fs < 8.0; fs += 1.0)
					{
						if (1.0 - fs / 8.0 < startY) break;
						xw += (fx_hash(vec2(sd, fs * 3.0 + 100.0)) - 0.5) * 0.06;
					}
					xw += (fx_hash(vec2(bs, 12.0)) > 0.5 ? 1.0 : -1.0) * 0.02;
					bw = 0.5;
					bb = 0.6;
				}
				float yRange = startY - fEndY;
				vec2 prev = vec2(xw, startY);
				for (float s = 1.0; s <= 16.0; s += 1.0)
				{
					float segY = startY - s / 16.0 * yRange;
					float jag = (fx_hash(vec2(bs, s * 3.0 + 100.0)) - 0.5) * 0.07 * uIntensity;
					jag += sin(s * 2.3 + sd * 5.0) * 0.015;
					xw += jag;
					vec2 cur = vec2(xw, segY);
					vec2 sg = cur - prev;
					float sl = length(sg);
					if (sl < 0.001)
					{
						prev = cur;
						continue;
					}
					vec2 sdir = sg / sl;
					float pj = clamp(dot(uv - prev, sdir), 0.0, sl);
					float d = length(uv - (prev + sdir * pj));
					lt += vec3(0.9, 0.9, 1.0) * (0.003 * bw / (d + 0.001) * bv * bb);
					lt += vec3(0.4, 0.4, 1.0) * (0.008 * bw / (d * d + 0.005) * bv * bb * 0.3);
					prev = cur;
				}
			}
		}
		color += clamp(lt, 0.0, 1.0);
	}
	else if (effect == 9)
	{
		float spawnRate = 0.9;
		float shellLife = 2.5;
		float latestShell = floor(t / spawnRate);
		vec3 fw = vec3(0.0, 0.0, 0.0);
		for (float k = 0.0; k < 6.0; k += 1.0)
		{
			float si = latestShell - k;
			if (si < 0.0) continue;
			float age = t - si * spawnRate;
			if (age > shellLife) continue;
			float sd = si + 0.5;
			float bx = 0.15 + fx_hash(vec2(sd, 1.0)) * 0.7;
			float by = 0.35 + fx_hash(vec2(sd, 2.0)) * 0.35;
			vec2 center = vec2(bx, by);
			float riseTime = 0.3;
			if (age < riseTime)
			{
				float rf = age / riseTime;
				vec2 rp = mix(vec2(bx, -0.05), center, rf * rf);
				vec2 diff = abs(uv - rp);
				fw += vec3(1.0, 1.0, 0.8) * step(diff.x, 0.008) * step(diff.y, 0.012);
				continue;
			}
			float ba = age - riseTime;
			float palIdx = floor(fx_hash(vec2(sd, 5.0)) * 5.0);
			vec3 col1 = vec3(1.0, 0.1, 0.2);
			vec3 col2 = vec3(1.0, 0.8, 0.0);
			if (palIdx < 1.0) { col1 = vec3(0.0, 1.0, 0.3); col2 = vec3(0.0, 0.6, 1.0); }
			else if (palIdx < 2.0) { col1 = vec3(1.0, 0.0, 1.0); col2 = vec3(0.3, 0.3, 1.0); }
			else if (palIdx < 3.0) { col1 = vec3(1.0, 0.5, 0.0); col2 = vec3(1.0, 1.0, 0.0); }
			else if (palIdx < 4.0) { col1 = vec3(0.0, 0.8, 1.0); col2 = vec3(1.0, 1.0, 1.0); }
			float flashSize = 0.06 * exp(-ba * 8.0) * uIntensity;
			vec2 cdiff = abs(uv - center);
			fw += vec3(1.0, 1.0, 0.9) * step(cdiff.x, flashSize) * step(cdiff.y, flashSize);
			if (ba < 0.3)
			{
				float starSize = 0.12 * (1.0 - ba / 0.3) * uIntensity;
				float cross1 = step(cdiff.x, 0.005) * step(cdiff.y, starSize);
				float cross2 = step(cdiff.y, 0.005) * step(cdiff.x, starSize);
				vec2 diag = abs(vec2(uv.x - center.x + uv.y - center.y, uv.x - center.x - uv.y + center.y)) * 0.707;
				float cross3 = step(diag.x, 0.004) * step(diag.y, starSize * 0.7);
				float cross4 = step(diag.y, 0.004) * step(diag.x, starSize * 0.7);
				fw += vec3(1.0, 1.0, 1.0) * (cross1 + cross2 + cross3 + cross4);
			}
			float fade = 1.0 - smoothstep(1.5, 2.2, ba);
			if (fade < 0.01) continue;
			float numArms = 3.0 + floor(fx_hash(vec2(sd, 6.0)) * 4.0);
			for (float p = 0.0; p < 24.0; p += 1.0)
			{
				float arm = floor(p / (24.0 / numArms));
				float armAng = arm * 6.28318 / numArms + fx_hash(vec2(sd, 20.0));
				float pInArm = mod(p, 24.0 / numArms);
				float spread = 0.05 + pInArm * 0.03;
				float ang = armAng + (fx_hash(vec2(sd, p + 30.0)) - 0.5) * 0.4;
				vec2 pdir = vec2(cos(ang), sin(ang));
				vec2 pp = center + pdir * uIntensity * spread * ba;
				pp.y -= 0.12 * ba * ba;
				float dotSize = 0.006 + 0.003 * fx_hash(vec2(sd, p + 50.0));
				vec2 pdiff = abs(uv - pp);
				float dot2 = step(pdiff.x, dotSize) * step(pdiff.y, dotSize);
				vec3 pc = mod(pInArm, 2.0) < 1.0 ? col1 : col2;
				float blink = step(0.0, sin(ba * 20.0 + p * 3.7));
				fw += pc * dot2 * fade * blink;
			}
			float ringR = ba * uIntensity * 0.4;
			float ringDist = abs(length(uv - center) - ringR);
			fw += mix(col1, col2, 0.5) * step(ringDist, 0.004) * fade * 0.7;
		}
		color += clamp(fw, 0.0, 1.0);
	}
	else if (effect == 10 || effect == 11)
	{
		float dotSize = 4.0 + uIntensity * 8.0;
		vec2 res = effect == 10 ? max(effectParams[1].xy, vec2(1.0, 1.0)) : vec2(1920.0, 1080.0);
		vec2 cell = floor(uv * res / dotSize) * dotSize / res;
		vec2 cellCenter = cell + (dotSize * 0.5) / res;
		vec3 sampled = fx_tex(cellCenter).rgb;
		float brightness = dot(sampled, vec3(0.299, 0.587, 0.114));
		float radius = brightness * 0.45 + 0.05;
		float dist = length(fract(uv * res / dotSize) - vec2(0.5, 0.5));
		float dot2 = 1.0 - step(radius, dist);
		color = effect == 10 ? sampled * dot2 : vec3(1.0, 0.7, 0.1) * brightness * dot2;
	}

	return color;
}

void main()
{
	vec4 original = texture2D(tex_base_color, v_texcoord0);
	int effect = int(effectParams[0].x + 0.5);
	vec3 color = original.rgb;
	if (effect != 0)
		color = fx_apply(v_texcoord0, effect, effectParams[0].y, effectParams[0].z, original);
	gl_FragColor = vec4(color, original.a) * staticColor_Alpha;
}
