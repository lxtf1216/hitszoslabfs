WORKDIR="./"

.PHONY:	train clean eval

train:
	@python data_setup.py $(WORKDIR)
	@python model.py $(WORKDIR)
	@python train.py $(WORKDIR)

eval:
	@python eval.py $(WORKDIR)

clean:
	# Note	this only cleans files generated in the current directory.
	# Should delete files manually when WORKDIR is not "./"
	@rm -rf __pycache__
	@rm -rf $(WORKDIR)trimmed
	@rm -rf $(WORKDIR)model