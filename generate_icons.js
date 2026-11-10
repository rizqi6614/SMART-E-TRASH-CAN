const fs = require('fs');
const zlib = require('zlib');
const path = require('path');

// CRC32 implementation for PNG chunks
function makeCRCTable() {
  let c;
  const crcTable = [];
  for (let n = 0; n < 256; n++) {
    c = n;
    for (let k = 0; k < 8; k++) {
      c = (c & 1) ? (0xedb88320 ^ (c >>> 1)) : (c >>> 1);
    }
    crcTable[n] = c;
  }
  return crcTable;
}

const crcTable = makeCRCTable();

function crc32(buf) {
  let crc = 0 ^ (-1);
  for (let i = 0; i < buf.length; i++) {
    crc = (crc >>> 8) ^ crcTable[(crc ^ buf[i]) & 0xff];
  }
  return (crc ^ (-1)) >>> 0;
}

function createChunk(type, data) {
  const len = data.length;
  const chunk = Buffer.alloc(12 + len);
  chunk.writeUInt32BE(len, 0);
  chunk.write(type, 4, 4, 'ascii');
  data.copy(chunk, 8);
  const typeAndData = chunk.subarray(4, 8 + len);
  chunk.writeUInt32BE(crc32(typeAndData), 8 + len);
  return chunk;
}

function createPNG(width, height, drawFn) {
  const rawData = Buffer.alloc(height * (1 + width * 4));
  let offset = 0;

  for (let y = 0; y < height; y++) {
    rawData[offset++] = 0; // Filter type 0 (None)
    for (let x = 0; x < width; x++) {
      const [r, g, b, a] = drawFn(x, y, width, height);
      rawData[offset++] = r;
      rawData[offset++] = g;
      rawData[offset++] = b;
      rawData[offset++] = a;
    }
  }

  const signature = Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);

  const ihdrData = Buffer.alloc(13);
  ihdrData.writeUInt32BE(width, 0);
  ihdrData.writeUInt32BE(height, 4);
  ihdrData[8] = 8; // 8 bit depth
  ihdrData[9] = 6; // RGBA color type
  ihdrData[10] = 0;
  ihdrData[11] = 0;
  ihdrData[12] = 0;
  const ihdrChunk = createChunk('IHDR', ihdrData);

  const compressedData = zlib.deflateSync(rawData, { level: 9 });
  const idatChunk = createChunk('IDAT', compressedData);
  const iendChunk = createChunk('IEND', Buffer.alloc(0));

  return Buffer.concat([signature, ihdrChunk, idatChunk, iendChunk]);
}

// Distance helper
function dist(x1, y1, x2, y2) {
  return Math.sqrt((x1 - x2) ** 2 + (y1 - y2) ** 2);
}

// Rounded box SDF
function roundedBoxSDF(x, y, cx, cy, w, h, r) {
  const dx = Math.abs(x - cx) - (w - r);
  const dy = Math.abs(y - cy) - (h - r);
  const outside = Math.sqrt(Math.max(dx, 0) ** 2 + Math.max(dy, 0) ** 2);
  const inside = Math.min(Math.max(dx, dy), 0);
  return outside + inside - r;
}

// Draw the Smart Bin Icon
function drawSmartBinIcon(x, y, W, H) {
  // Normalize coordinates 0 to 1
  const nx = x / W;
  const ny = y / H;
  const cx = 0.5;
  const cy = 0.5;

  // Squircle background SDF
  const squircleD = roundedBoxSDF(nx, ny, 0.5, 0.5, 0.46, 0.46, 0.16);

  if (squircleD > 0.005) {
    return [0, 0, 0, 0]; // Transparent outside
  }

  // Base background gradient: dark emerald
  const bgGrad = 1 - ny * 0.45;
  let r = Math.floor(6 * bgGrad);
  let g = Math.floor(22 * bgGrad + 8 * (1 - nx));
  let b = Math.floor(16 * bgGrad);
  let a = 255;

  // Outer border with neon green glow
  if (squircleD > -0.015 && squircleD <= 0.005) {
    const glow = 1 - Math.abs(squircleD - (-0.005)) / 0.01;
    r = Math.floor(r * (1 - glow) + 0 * glow);
    g = Math.floor(g * (1 - glow) + 245 * glow);
    b = Math.floor(b * (1 - glow) + 120 * glow);
  }

  // Inner subtle radar ring
  const ringD = Math.abs(dist(nx, ny, 0.5, 0.5) - 0.38);
  if (ringD < 0.003) {
    g = Math.min(255, g + 28);
    b = Math.min(255, b + 18);
  }

  // IoT Signal Waves at top (above bin lid: y around 0.14 to 0.28)
  const waveCy = 0.23;
  const waveD = dist(nx, ny, 0.5, waveCy);
  // Wave 1
  if (ny < waveCy && Math.abs(waveD - 0.065) < 0.007 && Math.abs(nx - 0.5) < 0.06) {
    return [0, 255, 136, 255];
  }
  // Wave 2
  if (ny < waveCy && Math.abs(waveD - 0.115) < 0.006 && Math.abs(nx - 0.5) < 0.11) {
    return [0, 229, 255, 230];
  }
  // Wave 3
  if (ny < waveCy && Math.abs(waveD - 0.165) < 0.005 && Math.abs(nx - 0.5) < 0.15) {
    return [0, 229, 255, 160];
  }
  // Antenna Node
  if (dist(nx, ny, 0.5, waveCy + 0.01) < 0.014) {
    return [0, 255, 136, 255];
  }

  // Smart Bin Lid: y from 0.30 to 0.37
  if (ny >= 0.28 && ny <= 0.36) {
    // Lid top bar
    const lidW = 0.22;
    if (Math.abs(nx - 0.5) < lidW) {
      if (ny >= 0.28 && ny <= 0.30 && Math.abs(nx - 0.5) < 0.05) {
        // Handle
        return [0, 230, 118, 255];
      }
      if (ny >= 0.31 && ny <= 0.36) {
        // Lid body with bevel highlight
        const lidHighlight = (0.36 - ny) / 0.05;
        const lr = Math.floor(0 + 40 * lidHighlight);
        const lg = Math.floor(200 + 55 * lidHighlight);
        const lb = Math.floor(100 + 40 * lidHighlight);
        return [lr, lg, lb, 255];
      }
    }
  }

  // Bin Main Body (Tapered Bucket): y from 0.38 to 0.82
  if (ny > 0.37 && ny <= 0.82) {
    // Tapering width: from 0.20 at top to 0.15 at bottom
    const t = (ny - 0.37) / (0.82 - 0.37);
    const halfWidth = 0.195 - t * 0.045;
    const dx = Math.abs(nx - 0.5);

    if (dx <= halfWidth) {
      // Bin edge highlight
      if (Math.abs(dx - halfWidth) < 0.008 || (ny > 0.805 && ny <= 0.82)) {
        return [0, 230, 118, 255];
      }

      // Vertical separation grooves (representing 3 bins)
      const groove1 = Math.abs(nx - (0.5 - halfWidth * 0.33));
      const groove2 = Math.abs(nx - (0.5 + halfWidth * 0.33));
      if (groove1 < 0.004 || groove2 < 0.004) {
        return [8, 22, 16, 255];
      }

      // Inside bin body fill: rich dark green
      let binR = 16 + Math.floor(10 * (1 - t));
      let binG = 46 + Math.floor(20 * (1 - t));
      let binB = 34 + Math.floor(12 * (1 - t));

      // Recycle Logo Symbol at Center (cy around 0.58)
      const rdx = nx - 0.5;
      const rdy = ny - 0.59;
      const rdist = Math.sqrt(rdx * rdx + rdy * rdy);

      // Outer ring of recycle arrows
      if (rdist >= 0.045 && rdist <= 0.075) {
        const angle = (Math.atan2(rdy, rdx) + Math.PI) % ((2 * Math.PI) / 3);
        if (angle > 0.3) {
          return [0, 255, 136, 255]; // Neon green arrows
        }
      }

      // Center glowing energy dot
      if (rdist < 0.015) {
        return [0, 229, 255, 255]; // Cyan core
      }

      return [binR, binG, binB, 255];
    }
  }

  // Status Indicator Dots below bin (y: 0.86)
  const dotY = 0.87;
  if (dist(nx, ny, 0.44, dotY) < 0.012) return [0, 230, 118, 255]; // Green dot
  if (dist(nx, ny, 0.50, dotY) < 0.012) return [255, 179, 0, 255];  // Amber dot
  if (dist(nx, ny, 0.56, dotY) < 0.012) return [255, 82, 82, 255];   // Red dot

  return [r, g, b, a];
}

console.log('Generating PWA Icons...');

const icon192 = createPNG(192, 192, drawSmartBinIcon);
fs.writeFileSync(path.join(__dirname, 'icons', 'icon-192.png'), icon192);
console.log('Generated icons/icon-192.png');

const icon512 = createPNG(512, 512, drawSmartBinIcon);
fs.writeFileSync(path.join(__dirname, 'icons', 'icon-512.png'), icon512);
console.log('Generated icons/icon-512.png');

const iconApple = createPNG(180, 180, drawSmartBinIcon);
fs.writeFileSync(path.join(__dirname, 'icons', 'apple-touch-icon.png'), iconApple);
console.log('Generated icons/apple-touch-icon.png');

// Also create favicon
const iconFavicon = createPNG(64, 64, drawSmartBinIcon);
fs.writeFileSync(path.join(__dirname, 'favicon.png'), iconFavicon);
console.log('Generated favicon.png');

console.log('All icons generated successfully!');
