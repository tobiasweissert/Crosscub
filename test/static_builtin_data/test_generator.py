"""Standalone lossless target-layout generator contracts."""
import importlib.util
from pathlib import Path
import struct
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('static_generator', ROOT / 'scripts/gen_static_builtin_data.py')
gen = None
if Path(SPEC.origin).exists():
    gen = importlib.util.module_from_spec(SPEC)
    SPEC.loader.exec_module(gen)


class NumericArrays(unittest.TestCase):
    def test_explicit_target_bytes(self):
        self.assertIsNotNone(gen, 'bounded generator is missing')
        self.assertEqual(gen.encode_array('EpdGlyph', '{{1,2,3,-4,5,6,0x1234},}'),
                         struct.pack('<BBHhhH2xI', 1, 2, 3, -4, 5, 6, 0x1234))
        self.assertEqual(gen.encode_array('EpdUnicodeInterval', '{{32,126,0},}'),
                         struct.pack('<III', 32, 126, 0))

    def test_comment_token_separation(self):
        for text in ['{{1/**/2,3,4},}', '{{0/**/x10,3,4},}']:
            with self.subTest(text=text), self.assertRaises(ValueError):
                gen.encode_array('EpdUnicodeInterval', text)
        self.assertEqual(gen.encode_array('EpdUnicodeInterval',
                         '{ /*row*/ {1/**/, /*field*/ 2,3} //end\n,}'),
                         struct.pack('<III', 1, 2, 3))

    def test_reject_unknown_syntax_and_ranges(self):
        for text in ['{}', '{{1,2,3}, junk}', '{{1,2,3+4},}',
                     '{{1,2,foo()},}', '{{-1,2,3},}', '{{1,2,4294967296},}',
                     '{{01,2,3},}', '{{1,2,3},} ; execute()']:
            with self.subTest(text=text), self.assertRaises(ValueError):
                gen.encode_array('EpdUnicodeInterval', text)
        with self.assertRaises(ValueError):
            gen.encode_array('EpdGlyph', '{{256,2,3,4,5,6,7},}')

    def test_all_76_canonical_font_arrays(self):
        import tempfile
        import json
        import hashlib
        import zlib
        self.assertTrue(hasattr(gen, 'generate_fonts'), 'font transformation missing')
        expected = json.loads((Path(__file__).parent / 'canonical.json').read_text())
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            inventory = gen.generate_fonts(ROOT, out, 'esp32-le-v1')
            self.assertEqual(len(inventory), 76)
            original_aggregator = (ROOT/'lib/EpdFont/builtinFonts/all.h').read_text()
            generated_aggregator = (out/'StaticBuiltinFonts.h').read_text()
            import re
            restored = re.sub(r'"static_font_declarations/(.*?)"', r'<builtinFonts/\1>', generated_aggregator)
            self.assertEqual(restored, original_aggregator, 'unlinked include inventory changed')
            for filename in sorted({Path(x['source']).name for x in inventory}):
                original = (ROOT/'lib/EpdFont/builtinFonts'/filename).read_text()
                transformed = (out/'static_fonts'/filename).read_text()
                transformed = transformed.replace(f'extern const EpdFontData {filename[:-2]} =',
                                                  f'static constexpr EpdFontData {filename[:-2]} =')
                skeleton = gen.ARRAY.sub('<METADATA>', original)
                restored = re.sub(r'static constexpr const (?:EpdGlyph|EpdUnicodeInterval) \(&\w+\)\[\d+\] = sb_\w+\.values;', '<METADATA>', transformed)
                self.assertEqual(restored, '#include "StaticBuiltinBacking.h"\n'+skeleton,
                                 'non-metadata source/license/bitmap/group/kerning/ligature/descriptor changed')
            self.assertEqual({x['name']: x['sha256'] for x in inventory}, expected)
            first = {str(p.relative_to(out)): p.read_bytes() for p in out.rglob('*') if p.is_file()}
            gen.generate_fonts(ROOT, out, 'esp32-le-v1')
            self.assertEqual(first, {str(p.relative_to(out)): p.read_bytes() for p in out.rglob('*') if p.is_file()})
            for x in inventory:
                self.assertEqual(hashlib.sha256(zlib.decompress((out / (x['name']+'.zlib')).read_bytes())).hexdigest(), x['sha256'])
            with self.assertRaises(ValueError):
                gen.generate_fonts(ROOT, out, 'native')

    def test_language_variant_exact_canonical_blobs(self):
        import json
        import tempfile
        import sys
        sys.path.insert(0, str(ROOT / 'scripts'))
        import gen_i18n as i18n
        self.assertTrue(hasattr(i18n, 'generate_static_variant'), 'I18n packing missing')
        languages, names, bcp, keys, translations, _ = i18n.load_translations(ROOT / 'lib/I18n/translations')
        used = i18n.find_used_string_keys([str(ROOT/'src'), str(ROOT/'lib')])
        keys = [key for key in keys if key in used]  # existing SCons strip-unused path
        translations = {key: translations[key] for key in keys}
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            i18n.generate_keys_header(languages, names, bcp, keys, str(out/'I18nKeys.h'))
            i18n.generate_strings_header(languages, str(out/'I18nStrings.h'))
            i18n.generate_strings_cpp(languages, names, keys, translations, str(out/'I18nStrings.cpp'))
            inventory = i18n.generate_static_variant(out)
            self.assertEqual({x['name']: x['sha256'] for x in inventory},
                             json.loads((Path(__file__).parent/'canonical-languages.json').read_text()))

    def test_packed_keys_whole_header_logical_guard(self):
        import re
        import sys
        import tempfile
        sys.path.insert(0, str(ROOT / 'scripts'))
        import gen_i18n as i18n
        languages, names, bcp, keys, translations, _ = i18n.load_translations(ROOT/'lib/I18n/translations')
        self.assertEqual(len(languages), 35)
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            i18n.generate_keys_header(languages, names, bcp, keys, str(out/'I18nKeys.h'))
            raw = (out/'I18nKeys.h').read_bytes()
            self.assertNotIn(b'CROSSCUB_PACKED_I18N_KEYS_H', raw)
            i18n.generate_strings_header(languages, str(out/'I18nStrings.h'))
            i18n.generate_strings_cpp(languages, names, keys, translations, str(out/'I18nStrings.cpp'))
            i18n.generate_static_variant(out)
            packed = (out/'I18nKeys.h').read_bytes()
            prefix = b'#ifndef CROSSCUB_PACKED_I18N_KEYS_H\n#define CROSSCUB_PACKED_I18N_KEYS_H\n'
            suffix = b'#endif  // CROSSCUB_PACKED_I18N_KEYS_H\n'
            self.assertTrue(packed.startswith(prefix), 'physical copies need one logical header identity')
            self.assertTrue(packed.endswith(suffix))
            guard = 'defined(CROSSPOINT_STATIC_BUILTINS) || defined(STATIC_BUILTIN_HOST_TEST)'
            expected = re.sub(r'extern const char (STRINGS_(?!EN_DATA)(\w+)_DATA)\[\];',
                              lambda m: f'#if {guard}\nextern const char* const {m[1]};\n#else\n{m[0]}\n#endif',
                              raw.decode()).encode()
            self.assertEqual(packed[len(prefix):-len(suffix)], expected,
                             'only whole-header guard may change existing packed keys bytes')
            self.assertTrue(expected.startswith(b'#pragma once\n'))
            # Partial locale input must still fail before changing any header.
            i18n.generate_strings_cpp(languages, names, keys, translations,
                                      str(out/'I18nStrings.cpp'), builtin={'EN'})
            with self.assertRaisesRegex(ValueError, 'all 35 languages'):
                i18n.generate_static_variant(out)
            self.assertEqual((out/'I18nKeys.h').read_bytes(), packed)

    def test_private_bundle_has_single_transaction_owner(self):
        self.assertTrue(hasattr(gen, 'generate'), 'single boot transaction generator missing')
        import tempfile
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            gen.generate(ROOT, out, 'esp32-le-v1')
            self.assertTrue((out/'StaticBuiltinOwner.cpp').exists())
            self.assertTrue((out/'I18nStrings.cpp').exists())


if __name__ == '__main__':
    unittest.main(verbosity=2)
