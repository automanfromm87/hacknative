// ===== 3D Raytraced Sphere — HackNative + SDL2 =====

require "sdl2.hhi";
function sqrt(float $x): float;

<<__EntryPoint>>
async function main(): Awaitable<void> {
  // --- Init SDL2 ---
  $sinit = SDL_Init(32);
  $window = SDL_CreateWindow("HackNative 3D Sphere", 805306368, 805306368, 640, 480, 4);
  $ren = SDL_CreateRenderer($window, -1, 2);

  // --- Scene setup ---
  // Camera at (0, 0, 3), looking down -Z
  // Sphere at origin, radius 1.2
  // Light direction (normalized ~): (0.5, 0.7, 1.0)
  $camZ = 3.0;
  $sphereR = 1.2;
  $r2 = $sphereR * $sphereR;

  // Light direction (will normalize)
  $lx = 0.5;
  $ly = 0.7;
  $lz = 1.0;
  $llen = sqrt($lx * $lx + $ly * $ly + $lz * $lz);
  $lx = $lx / $llen;
  $ly = $ly / $llen;
  $lz = $lz / $llen;

  // --- Clear to dark gradient background ---
  $rc = SDL_SetRenderDrawColor($ren, 15, 15, 30, 255);
  $rc = SDL_RenderClear($ren);

  // --- Draw background gradient ---
  for ($bgY = 0; $bgY < 480; $bgY = $bgY + 1) {
    $bgF = $bgY * 1.0 / 480.0;
    $bgR = 10.0 + $bgF * 30.0;
    $bgG = 10.0 + $bgF * 20.0;
    $bgB = 30.0 + $bgF * 40.0;
    $rc = SDL_SetRenderDrawColor($ren, $bgR, $bgG, $bgB, 255);
    for ($bgX = 0; $bgX < 640; $bgX = $bgX + 1) {
      $rc = SDL_RenderDrawPoint($ren, $bgX, $bgY);
    }
  }

  // --- Raytrace sphere ---
  for ($py = 0; $py < 480; $py = $py + 1) {
    for ($px = 0; $px < 640; $px = $px + 1) {
      // NDC: map pixel to [-aspect, aspect] x [-1, 1]
      $u = ($px * 2.0 - 640.0) / 480.0;
      $v = (480.0 - $py * 2.0) / 480.0;

      // Ray: origin=(0,0,camZ), direction=(u, v, -1) (unnormalized)
      $dx = $u;
      $dy = $v;
      $dz = 0.0 - 1.0;

      // Sphere at origin: |P + t*D|^2 = r^2
      // oc = origin - sphere_center = (0, 0, camZ)
      $ocz = $camZ;

      // Quadratic coefficients
      $a = $dx * $dx + $dy * $dy + $dz * $dz;
      $b = 2.0 * ($dz * $ocz);
      $c = $ocz * $ocz - $r2;

      $disc = $b * $b - 4.0 * $a * $c;

      if ($disc > 0.0) {
        // Hit! Find intersection
        $sqrtDisc = sqrt($disc);
        $t = (0.0 - $b - $sqrtDisc) / (2.0 * $a);

        if ($t > 0.0) {
          // Hit point
          $hx = $t * $dx;
          $hy = $t * $dy;
          $hz = $camZ + $t * $dz;

          // Normal = hit / radius (sphere at origin)
          $nx = $hx / $sphereR;
          $ny = $hy / $sphereR;
          $nz = $hz / $sphereR;

          // Diffuse shading: dot(normal, light)
          $diff = $nx * $lx + $ny * $ly + $nz * $lz;
          if ($diff < 0.0) {
            $diff = 0.0;
          }

          // Specular (Blinn-Phong): half vector ≈ light (view is ~(0,0,1))
          $hVx = $lx;
          $hVy = $ly;
          $hVz = $lz + 1.0;
          $hLen = sqrt($hVx * $hVx + $hVy * $hVy + $hVz * $hVz);
          $hVx = $hVx / $hLen;
          $hVy = $hVy / $hLen;
          $hVz = $hVz / $hLen;
          $spec = $nx * $hVx + $ny * $hVy + $nz * $hVz;
          if ($spec < 0.0) {
            $spec = 0.0;
          }
          // spec^16 approximation: square 4 times
          $spec = $spec * $spec;
          $spec = $spec * $spec;
          $spec = $spec * $spec;
          $spec = $spec * $spec;

          // Fresnel-like rim effect
          $rim = 1.0 - ($nx * 0.0 + $ny * 0.0 + $nz * 1.0);
          if ($rim < 0.0) {
            $rim = 0.0;
          }
          $rim = $rim * $rim;

          // Final color: ambient + diffuse + specular + rim
          $cr = 30.0 + $diff * 180.0 + $spec * 255.0 + $rim * 40.0;
          $cg = 50.0 + $diff * 120.0 + $spec * 255.0 + $rim * 60.0;
          $cb = 80.0 + $diff * 80.0 + $spec * 255.0 + $rim * 80.0;

          // Clamp to 255
          if ($cr > 255.0) { $cr = 255.0; }
          if ($cg > 255.0) { $cg = 255.0; }
          if ($cb > 255.0) { $cb = 255.0; }

          $rc = SDL_SetRenderDrawColor($ren, $cr, $cg, $cb, 255);
          $rc = SDL_RenderDrawPoint($ren, $px, $py);
        }
      }
    }
  }

  SDL_RenderPresent($ren);
  echo "3D sphere rendered — close window to exit\n";

  // --- Event loop ---
  $quit = 0;
  while ($quit == 0) {
    $quit = hack_sdl_poll_quit();
    SDL_Delay(16);
  }

  SDL_DestroyRenderer($ren);
  SDL_DestroyWindow($window);
  SDL_Quit();
  return 0;
}
