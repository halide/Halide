Get-ChildItem ../../build/bin/Release -filter correctness*.exe | ForEach { 
  echo ""
  echo $_.Fullname
  &$_.Fullname 
  if ($LastExitCode) {
    echo "Test failed!"
    exit $LastExitCode
  }
}

Get-ChildItem ../../build/bin/Release -filter performance*.exe | ForEach { 
  echo ""
  echo $_.Fullname
  &$_.Fullname 
  if ($LastExitCode) {
    echo "Test failed!"
    exit $LastExitCode
  }
}

cmd /c pause
