require 'benchmark'
require 'binding_of_caller'

def failit
  1 / 0
end

def test
  tracepoint = TracePoint.new(:raise) do |tp|
    tp.raised_exception.instance_variable_set(
      :@__bindings, Kernel.binding.callers)
  end

  Benchmark.bm do |x|
    n = 100000

    x.report('boc') do
      tracepoint.enable do
        n.times do
          begin
            failit
          rescue
          end
        end
      end
    end

    x.report('normal') do
      n.times do
        begin
          failit
        rescue
        end
      end
    end
  end
end

test
