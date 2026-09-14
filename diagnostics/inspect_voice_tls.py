from pathlib import Path
root=Path('/tmp/v3-desktop-voice-20260915/standalone-tls')
out=Path(__file__).resolve().parent/'voice-reference/tls'
out.mkdir(parents=True,exist_ok=True)
for p in [root/'include/mbedtls/mbedtls_config.h',root/'tf-psa-crypto/include/psa/crypto_config.h',
          root/'tf-psa-crypto/include/mbedtls/mbedtls_config.h',root/'tf-psa-crypto/include/mbedtls/private/crypto_config.h']:
    if p.is_file():
        (out/(p.parent.name+'-'+p.name)).write_bytes(p.read_bytes())
        print(p.relative_to(root))
        print('\n'.join(s for s in p.read_text().splitlines() if s.startswith('#define ')))
