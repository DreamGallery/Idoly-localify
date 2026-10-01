"""Host checks for capture accounting and the real Android JSON status consumer."""
from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CaptureSessionTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('c++'), 'C++ compiler required')
    def test_native_completeness_and_frozen_session(self):
        fixture = r'''
#include "capture_session_state.h"
#include <cassert>
int main() {
    CaptureSessionState s;
    int writes = 0;
    auto good = [&](const std::string&) { ++writes; return true; };
    assert(!s.complete()); // No session after process restart.
    s.begin("text-1.jsonl");
    assert(s.append(false, "abc\n", good));
    assert(s.append(true, "def\n", good));
    assert(!s.complete() && s.bytes == 8 && s.textRecords == 1 && s.layoutRecords == 1);
    s.stop();
    assert(s.complete());
    assert(!s.append(false, "late", good) && writes == 2 && s.bytes == 8);
    assert(s.droppedRecords == 0); // Stop freezes counters too.
    s.begin("text-2.jsonl");
    assert(s.bytes == 0 && s.textRecords == 0 && !s.truncated);
    assert(s.append(false, std::string(CaptureSessionState::kLimit, 'x'), good));
    assert(!s.append(true, "x", good));
    assert(s.truncated && !s.writeFailed && s.droppedRecords == 1 && writes == 3);
    s.stop();
    assert(!s.complete());
    s.begin("text-3.jsonl");
    auto fail = [&](const std::string&) { ++writes; return false; };
    assert(!s.append(false, "partial disk write", fail));
    assert(s.bytes == 0 && s.textRecords == 0 && s.writeFailed);
    assert(!s.append(true, "later", good) && writes == 4); // No repeated failing I/O.
    s.stop();
    assert(!s.complete() && s.droppedRecords == 2);
    s.begin("text-4.jsonl");
    assert(!s.writeFailed && !s.truncated && s.droppedRecords == 0);
    assert(s.append(true, "new", good));
    s.stop();
    assert(s.complete() && s.textFile == "text-4.jsonl" && s.bytes == 3);
    CaptureSessionState restarted;
    assert(restarted.textFile.empty() && !restarted.complete());
}
'''
        with tempfile.TemporaryDirectory() as directory:
            source, binary = Path(directory)/'test.cpp', Path(directory)/'test'
            source.write_text(fixture)
            subprocess.run(['c++', '-std=c++17', '-I', str(ROOT/'app/src/main/cpp'),
                            str(source), '-o', str(binary)], check=True, capture_output=True)
            subprocess.run([str(binary)], check=True, capture_output=True)

    def test_java_status_rejects_wrong_session_and_preserves_partial_state(self):
        sdk = Path(os.environ.get('ANDROID_HOME', Path.home()/'Library/Android/sdk'))
        sources = sorted((sdk/'sources').glob('android-*/org/json'))
        jdk = Path(os.environ.get('JAVA_HOME', '/Applications/Android Studio.app/Contents/jbr/Contents/Home'))
        javac, java = jdk/'bin/javac', jdk/'bin/java'
        if not sources or not javac.is_file():
            self.skipTest('Installed Android SDK sources and JDK required')
        fixture = r'''
package io.github.dreamgallery.idoly.localify;
import org.json.JSONObject;
public class CaptureStatusFixture {
    static JSONObject state() throws Exception {
        return new JSONObject().put("schema_version",1).put("text_file","text-1.jsonl")
            .put("stopped",true).put("complete",true).put("truncated",false)
            .put("write_failed",false).put("dropped_records",0).put("bytes_written",8)
            .put("text_records",1).put("layout_records",1);
    }
    static JSONObject check(JSONObject state, String file, long text, long layout) throws Exception {
        return CaptureStatus.forExport(state.toString(),file,text,layout);
    }
    static void rejected(JSONObject s, String file) throws Exception {
        try { check(s,file,4,4); throw new AssertionError("accepted invalid session"); }
        catch(java.io.IOException expected) {}
    }
    public static void main(String[] args) throws Exception {
        JSONObject s = check(state(),"text-1.jsonl",4,4);
        assert s.getBoolean("complete") && CaptureStatus.warning(s).isEmpty();
        s = check(state().put("complete",false).put("truncated",true).put("dropped_records",1),
            "text-1.jsonl",4,4);
        assert !s.getBoolean("complete") && CaptureStatus.warning(s).contains("16 MiB");
        s = check(state().put("complete",false).put("write_failed",true),"text-1.jsonl",4,5);
        assert !s.getBoolean("complete") && s.getBoolean("file_size_mismatch");
        s = check(state(),"text-1.jsonl",4,3);
        assert !s.getBoolean("complete"); // File changed after native accounting.
        s = check(state().put("complete",false),"text-1.jsonl",4,4);
        assert !s.getBoolean("complete"); // Never promote unknown failure to success.
        rejected(state(),"text-2.jsonl");
        rejected(state().put("stopped",false),"text-1.jsonl");
        try { CaptureStatus.forExport("","text-1.jsonl",0,0); throw new AssertionError(); }
        catch(java.io.IOException expected) {}
        // Android serializes integral doubles as integers; font threshold editing is valid.
        assert new JSONObject().put("n",6.0).toString().equals("{\"n\":6}");
    }
}
'''
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            inputs = []
            # Use the installed Android org.json implementation. Remove only platform
            # annotations unavailable on a host JVM, not executable serialization code.
            for source in sources[-1].glob('*.java'):
                text = source.read_text()
                text = re.sub(r'^import (?:static )?(?:android\.|libcore\.).*;\n', '', text, flags=re.M)
                text = re.sub(r'@(?:UnsupportedAppUsage|NonNull|Nullable)\b', '', text)
                text = re.sub(r'@SystemApi\([^)]*\)', '', text)
                target = temp/source.name
                target.write_text(text)
                inputs.append(str(target))
            inputs.append(str(ROOT/'app/src/main/java/io/github/dreamgallery/idoly/localify/CaptureStatus.java'))
            target = temp/'CaptureStatusFixture.java'
            target.write_text(fixture)
            inputs.append(str(target))
            subprocess.run([str(javac), '-d', str(temp), *inputs], check=True, capture_output=True)
            subprocess.run([str(java), '-ea', '-cp', str(temp),
                            'io.github.dreamgallery.idoly.localify.CaptureStatusFixture'],
                           check=True, capture_output=True)


if __name__ == '__main__':
    unittest.main()
