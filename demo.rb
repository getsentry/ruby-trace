require "rbtrace"
require 'benchmark'

def bar
  1/0
end

def foo a, b, pass=false
  x = "#{a} + #{b} = #{a + b}"
  bar unless pass
  x
end

def test
  RbTrace.capture_stack do
    foo 1, 2
  end
end

def bench
  Benchmark.bmbm do |x|
    n = 200000

    x.report('exc:normal') {
      n.times do
        begin
          foo 1, 2
        rescue
        end
      end
    }

    x.report('exc:capture') {
      RbTrace.make_tracepoint.enable do
        n.times do
          begin
            foo 1, 2
          rescue
          end
        end
      end
    }

    x.report('pass:normal') {
      n.times do
        foo 1, 2, true
      end
    }

    x.report('pass:capture') {
      RbTrace.make_tracepoint.enable do
        n.times do
          begin
            foo 1, 2, true
          rescue
          end
        end
      end
    }
  end
end
