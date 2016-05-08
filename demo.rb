require "rbtrace"
require 'benchmark'

def bar
  1/0
end

def do_math a, b
  x = "#{a} + #{b} = #{a + b}"
  x
end

def foo pass=false, depth=100
  if depth == 0
    bar unless pass
  else
    foo(pass, depth - 1)
  end
end

def test
  RbTrace.capture_stack do
    foo
  end
end

def bench
  Benchmark.bmbm do |x|
    n = 200000

    x.report('exc:normal') {
      n.times do
        begin
          foo
        rescue
        end
      end
    }

    x.report('exc:capture') {
      RbTrace.make_tracepoint.enable do
        n.times do
          begin
            foo
          rescue
          end
        end
      end
    }

    x.report('pass:normal') {
      n.times do
        foo true
      end
    }

    x.report('pass:capture') {
      RbTrace.make_tracepoint.enable do
        n.times do
          begin
            foo true
          rescue
          end
        end
      end
    }
  end
end
