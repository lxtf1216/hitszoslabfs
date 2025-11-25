# Train a new model with data already created by data_setup.py and save it.
import engine
import utils
import model as m
import torch

LR = 0.1
EPOCHS = 5

path_prefix = "./"

def main():
    model_path = f"{path_prefix}model/model.pth"
    
    train_set, test_set = utils.merge_subsets(10, f"{path_prefix}trimmed")
    train_loader = utils.load(train_set)
    test_loader = utils.load(test_set)
    
    utils.info("train.py: data loaded")
    
    device = 'cpu'# "cuda" if torch.cuda.is_available() else "cpu"
    model = m.TinyVGG()
    model.load_state_dict(torch.load(model_path))
    model.to(device)
    utils.info(f"train.py: using device: {device}")
    
    engine.train(
        model,
        train_loader,
        test_loader,
        torch.nn.CrossEntropyLoss(),
        torch.optim.SGD(model.parameters(), lr=LR),
        device=device,
        epochs=EPOCHS,
    )
    
    utils.info("train.py: training complete")
    
    torch.save(model.state_dict(), model_path)
    
if __name__ == "__main__":
    import sys, os
    path_prefix = f"{sys.argv[1]}/" if len(sys.argv) > 1 else "./"
    
    main()